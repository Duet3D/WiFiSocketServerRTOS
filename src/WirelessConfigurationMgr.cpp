#include "WirelessConfigurationMgr.h"

#include <cstring>
#include <sys/fcntl.h>

#include "esp_system.h"
#include "rom/ets_sys.h"
#include "esp_spiffs.h"

#include "Config.h"

#if ESP8266
#include "esp8266/partition.h"
#endif

WirelessConfigurationMgr* WirelessConfigurationMgr::instance = nullptr;

void WirelessConfigurationMgr::Init()
{
	// This class manages two partitions: a credential scratch partition and
	// the key-value storage (KVS) partition.
	//
	// The scratch partition is a raw partition that provides the required contiguous memory
	// for enterprise network credentials. Credentials stored in the KVS are copied to this
	// partition before being passed to ESP WPA2 enterprise APIs.
	//
	// The key-value storage partition uses SPIFFS. It is used to store wireless configuration data,
	// the credentials, and some other bits and pieces. There are three main 'directories':
	// 		- ssids - stores wireless configuration data, with key/path 'ssids/xx' where xx is the ssid slot
	// 		- creds - stores credential for a particular wireless config data stored in 'ssids', with key/path
	// 					'creds/xx/yy', where xx is the ssid slot, yy is the credential index
	// 		- scratch - stores some values related to the scratch partition, with key/path 'scratch/ss' where
	// 					ss is the string id
	//
	esp_vfs_spiffs_conf_t conf = {
		.base_path = "/kvs",
		.partition_label = NULL,
		.max_files = 1,
		.format_if_mount_failed = true
	};

	esp_vfs_spiffs_register(&conf);

	// Memory map the partition, remembering the base pointer for the lifetime of the app.
	spi_flash_mmap_handle_t mapHandle;
	scratchPartition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, SCRATCH_NS);
	esp_partition_mmap(scratchPartition, 0, scratchPartition->size, SPI_FLASH_MMAP_DATA,
						reinterpret_cast<const void**>(&scratchBase), &mapHandle);

	// Check if first time and the storage should be initialized. The marker here is SSID slot 0,
	// since WirelessConfigurationMgr::Reset works it's way backwards to it.
	if (!GetKV(GetSsidKey(0), nullptr, 0))
	{
		debugPrintf("initializing SSID storage...");
		Reset();

		// Restore SSID info from old, 1.x firmware, if the partition exists
		const esp_partition_t* oldSsids = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
			ESP_PARTITION_SUBTYPE_DATA_NVS, "ssids_old");

		if (oldSsids) {
			debugPrintf("restoring old SSID...");
			const size_t eepromSizeNeeded = (MaxRememberedNetworks + 1) * sizeof(WirelessConfigurationData);

			uint8_t *buff = reinterpret_cast<uint8_t*>(malloc(eepromSizeNeeded));
			memset(buff, 0xFF, eepromSizeNeeded);
			esp_partition_read(oldSsids, 0, buff, eepromSizeNeeded);

			WirelessConfigurationData *data = reinterpret_cast<WirelessConfigurationData*>(buff);
			for (int i = MaxRememberedNetworks; i >= 0; i--) {
				WirelessConfigurationData *d = &(data[i]);
				if (d->ssid[0] != 0xFF) {
					SetSsidData(i, *d);
				}
			}

			free(buff);
			debugPrintf("done!\n");
		}
	}

	// Storing an enterprise SSID and its credentials might not have
	// gone all the way. Since credentials are stored first before the
	// SSID data, if credentials are incompletely stored due to a power loss,
	// we can detect and clean up those orphaned credentials here.
	for (int i = MaxRememberedNetworks; i > 0; i--)
	{
		WirelessConfigurationData temp;
		if ((GetSsid(i, temp) && IsSsidBlank(temp)))
		{
			EraseCredential(i);
		}
	}
}

void WirelessConfigurationMgr::Reset()
{
	// Reset storage and reset values to default.
	//  - clear the scratch partition, and the associated scratch
	//		key-value pairs
	// 	- the SSID slot must be reset to a blank value, and credentials for
	// 		each slot must be cleared
	//
	// Work down to SSID slot 0, since it is used to detect whether
	// the KVS has been initialized for the first time.
	EraseScratch();

	for (int i = MaxRememberedNetworks; i >= 0; i--)
	{
		// Erase the SSID first, then the credentials. This is because if
		// erasure of credentials is incomplete, if the corresponding
		// SSID has been cleared first, then it can be erased at startup
		EraseSsid(i);
		EraseCredential(i);
	}
}

int WirelessConfigurationMgr::SetSsid(const WirelessConfigurationData& data, bool ap = false)
{
	WirelessConfigurationData temp;
	memset(&temp, 0, sizeof(temp));

	int ssid = WirelessConfigurationMgr::AP;

	if (!ap)
	{
		ssid = GetSsid(data.ssid, temp);

		if (ssid < 0)
		{
			ssid = FindEmptySsidEntry();
			if (ssid == WirelessConfigurationMgr::AP) { // reserved for AP details
				ssid = -1;
			}
		}
	}

	if (ssid >= 0)
	{
		// This might previously be an enterprise ssid, delete
		// its credentials here.
		if (temp.eap.protocol == EAPProtocol::NONE || (EraseSsid(ssid) && EraseCredential(ssid)))
		{
			if (SetSsidData(ssid, data))
			{
				return ssid;
			}
		}

		ssid = -1;
	}

	return ssid;
}

bool WirelessConfigurationMgr::EraseSsid(int ssid)
{
	if (ssid >= 0)
	{
		if (ResetIfCredentialsLoaded(ssid))
		{
			if (EraseSsidData(ssid))
			{
				return true;
			}
		}
	}

	return false;
}

bool WirelessConfigurationMgr::EraseSsid(const char *ssid)
{
	WirelessConfigurationData temp;
	return EraseSsid(GetSsid(ssid, temp));
}

bool WirelessConfigurationMgr::GetSsid(int ssid, WirelessConfigurationData& data)
{
	if (ssid <= MaxRememberedNetworks)
	{
		return GetKV(GetSsidKey(ssid), &data, sizeof(data));
	}

	return false;
}

int WirelessConfigurationMgr::GetSsid(const char *ssid, WirelessConfigurationData& data)
{
	if (ssid)
	{
		for (int i = MaxRememberedNetworks; i >= 0; i--)
		{
			WirelessConfigurationData temp;
			if (GetSsid(i, temp) && strncmp(ssid, temp.ssid, sizeof(temp.ssid)) == 0)
			{
				data = temp;
				return i;
			}
		}
	}
	return -1;
}

bool WirelessConfigurationMgr::BeginEnterpriseSsid(const WirelessConfigurationData &data)
{
	// Personal network assumed unless otherwise stated. PSK is indicated by WirelessConfigurationData::eap.protocol == 1,
	// which is the null terminator for the pre-shared key. Enforce that here.
	static_assert(offsetof(WirelessConfigurationData, eap.protocol) ==
					offsetof(WirelessConfigurationData,
							password[sizeof(data.password) - sizeof(data.eap.protocol)]));

	// Check if the credentials will fit
	size_t total = 0;
	const uint32_t *credsSizes = reinterpret_cast<const uint32_t*>(&(data.eap.credSizes));

	for(int cred = 0; cred < sizeof(CredentialsInfo)/sizeof(uint32_t); cred++)
	{
		total += credsSizes[cred];
	}

	if (total <= FreeKV() && total <= scratchPartition->size)
	{
		WirelessConfigurationData temp;
		int ssid = GetSsid(data.ssid, temp);

		if (ssid < 0)
		{
			ssid = FindEmptySsidEntry();
		}

		if (ssid > 0)
		{
			if (EraseSsid(ssid))
			{
				pendingSsid = static_cast<PendingEnterpriseSsid*>(calloc(1, sizeof(PendingEnterpriseSsid)));
				if (pendingSsid)
				{
					pendingSsid->data = data;
					pendingSsid->ssid = ssid;
					return true;
				}
			}
		}
	}

	return false;
}

bool WirelessConfigurationMgr::SetEnterpriseCredential(int cred, const void* buff, size_t size)
{
	if (pendingSsid)
	{
		uint32_t *credsSizes = reinterpret_cast<uint32_t*>(&(pendingSsid->sizes));
		uint32_t *pendingSizes = reinterpret_cast<uint32_t*>(&(pendingSsid->data.eap.credSizes));

		size_t newSize = credsSizes[cred] + size;

		if (newSize <= pendingSizes[cred])
		{
			if(SetKV(GetCredentialKey(pendingSsid->ssid, cred), buff, size, credsSizes[cred]))
			{
				credsSizes[cred] = newSize;
				return true;
			}
		}
	}

	return false;
}

bool WirelessConfigurationMgr::EndEnterpriseSsid(bool cancel = true)
{
	bool ok = false;

	if (pendingSsid)
	{
		if (!cancel)
		{
			// Make sure that the sizes sent at the beginning matches
			// what we have received.
			uint32_t *credsSizes = reinterpret_cast<uint32_t*>(&(pendingSsid->sizes));
			uint32_t *pendingSizes = reinterpret_cast<uint32_t*>(&(pendingSsid->data.eap.credSizes));

			ok = true;

			for(int cred = 0; ok && cred < sizeof(CredentialsInfo) / sizeof(uint32_t); cred++)
			{
				ok = ((pendingSizes[cred] == credsSizes[cred]));

				if (ok && !credsSizes[cred])
				{
					ok = EraseCredential(pendingSsid->ssid, cred);
				}
			}

			if (ok)
			{
				ok = SetSsidData(pendingSsid->ssid, pendingSsid->data);
			}
			else
			{
				EraseCredential(pendingSsid->ssid);
			}
		}
		else
		{
			ok = EraseCredential(pendingSsid->ssid);
		}

		free(pendingSsid);
		pendingSsid = nullptr;
	}

	return ok;
}

const uint8_t* WirelessConfigurationMgr::GetEnterpriseCredentials(int ssid, const CredentialsInfo& sizes, CredentialsInfo& offsets)
{
	const uint8_t *res = nullptr;

	// Check that all information needed for the proceeding operations can
	// be retrieved.
	uint32_t loadedSsid = 0, baseOffset = 0;

	if (GetKV(GetScratchKey(LOADED_SSID_ID), &loadedSsid, sizeof(loadedSsid)) &&
		GetKV(GetScratchKey(SCRATCH_OFFSET_ID), &baseOffset, sizeof(baseOffset)))
	{
		// Get the total size of credentials
		const uint32_t *sizesArr = reinterpret_cast<const uint32_t*>(&sizes);
		size_t totalSize = 0;
		for(int cred = 0; cred < sizeof(sizes)/sizeof(sizesArr[0]); cred++)
		{
			totalSize += sizesArr[cred];
		}

		// If the SSID has already been loaded, just return the existing pointer
		// and compute offsets. If not, load it in the scratch partition.
		if (loadedSsid == ssid)
		{
			uint32_t *offsetsArr = reinterpret_cast<uint32_t*>(&offsets);

			for(int cred = 0, offset = 0; cred < sizeof(offsets)/sizeof(offsetsArr[0]); cred++)
			{
				offsetsArr[cred] = offset;
				offset += sizesArr[cred];
			}

			res = (scratchBase + baseOffset - totalSize);
		}
		else
		{
			bool ok = true;

			// Increment the offset first. If it will not fit, start from the top
			// again.
			if (baseOffset + totalSize > scratchPartition->size)
			{
				baseOffset = 0;
				esp_err_t err = esp_partition_erase_range(scratchPartition, baseOffset, scratchPartition->size);
				ok = (err == ESP_OK);
			}

			if (ok)
			{
				uint32_t newOffset = baseOffset + totalSize;
				ok = SetKV(GetScratchKey(SCRATCH_OFFSET_ID), &newOffset, sizeof(newOffset));

				if (ok)
				{
					// Store offsets from the base offset
					uint32_t *offsetsArr = reinterpret_cast<uint32_t*>(&offsets);
					uint8_t *buff = static_cast<uint8_t*>(malloc(MaxCredentialChunkSize));

					for(int cred = 0, offset = 0; ok && cred < sizeof(offsets)/sizeof(offsetsArr[0]); cred++)
					{
						offsetsArr[cred] = offset;

						for(int sz = 0, pos = 0, remain = sizesArr[cred];
							ok && remain > 0; remain -= sz, offset += sz, pos += sz)
						{
							memset(buff, 0, MaxCredentialChunkSize);

							sz = (remain >= MaxCredentialChunkSize) ? MaxCredentialChunkSize : remain;
							if (GetKV(GetCredentialKey(ssid, cred), buff, sz, pos))
							{
								esp_err_t err = esp_partition_write(scratchPartition, baseOffset + offset, buff, sz);
								ok = (err == ESP_OK);
							}
							else
							{
								ok = false;
							}
						}
					}

					free(buff);

					if (ok)
					{
						loadedSsid = ssid;
						ok = SetKV(GetScratchKey(LOADED_SSID_ID), &loadedSsid, sizeof(loadedSsid));

						if (ok)
						{
							res = scratchBase + baseOffset;
						}
					}
				}
			}
		}
	}

	return res;
}

bool WirelessConfigurationMgr::DeleteKV(std::string key)
{
	std::string path = "/kvs/";
	path.append(key);

	// Faster than actually deleting the file, for the cost
	// of a few more bytes maintaing the empty file.
	int f = open(path.c_str(), O_WRONLY | O_TRUNC | O_CREAT);

	if (f >= 0)
	{
		close(f);
		return true;
	}

	return false;
}

bool WirelessConfigurationMgr::SetKV(std::string key, const void *buff, size_t sz, bool append)
{
	if (buff && sz)
	{
		std::string path = "/kvs/";
		errno = 0;
		path.append(key);

		int f = open(path.c_str(), O_WRONLY | (append ? O_APPEND : O_CREAT | O_TRUNC));

		if (f >= 0)
		{
			size_t written = write(f, buff, sz);
			close(f);
			return written == sz;
		}
	}

	return false;
}

bool WirelessConfigurationMgr::GetKV(std::string key, void* buff, size_t sz, size_t pos)
{
	std::string path = "/kvs/";
	errno = 0;
	path.append(key);

	int f = open(path.c_str(), O_RDONLY);

	if (f >= 0)
	{
		bool res = true;

		// If buff == NULL or sz == 0, this command is only used to check
		// if the particular key exists. Therefore, do nothing
		// but close the opened file.
		if (buff && sz)
		{
			res = (lseek(f, pos, SEEK_SET) == pos);

			if (res)
			{
				res = (read(f, buff, sz) == sz);
			}
		}

		close(f);
		return res;
	}

	return false;
}

size_t WirelessConfigurationMgr::FreeKV()
{
	size_t total = 0, used = 0;
	esp_err_t ret = esp_spiffs_info(NULL, &total, &used);
	return (ret == ESP_OK && used < total) ? total - used : 0;
}

std::string WirelessConfigurationMgr::GetSsidKey(int ssid)
{
	std::string res = SSIDS_NS;
	res.append("/");
	res.append(std::to_string(ssid));
	return res;
}

bool WirelessConfigurationMgr::SetSsidData(int ssid, const WirelessConfigurationData& data)
{
	if (ssid <= MaxRememberedNetworks)
	{
		return SetKV(GetSsidKey(ssid), &data, sizeof(data));
	}

	return false;
}

bool WirelessConfigurationMgr::EraseSsidData(int ssid)
{
	WirelessConfigurationData clean;
	memset(&clean, 0xFF, sizeof(clean));
	return SetSsidData(ssid, clean);
}

std::string WirelessConfigurationMgr::GetScratchKey(const char* name)
{
	std::string res = SCRATCH_NS;
	res.append("/");
	res.append(name);
	return res;
}

bool WirelessConfigurationMgr::EraseScratch()
{
	// Erase the scratch partition first, then the scratch key-value pairs.
	// The reason for this is that if the scratch partition is interrupted
	// and does not reach the current offset, the memory from the current
	// offset will still be valid to write to.
	esp_err_t err = esp_partition_erase_range(scratchPartition, 0, scratchPartition->size);

	if (err == ESP_OK)
	{
		uint32_t zero = 0;
		return SetKV(GetScratchKey(LOADED_SSID_ID), &zero, sizeof(zero)) &&
				SetKV(GetScratchKey(SCRATCH_OFFSET_ID), &zero, sizeof(zero));
	}

	return false;
}

std::string WirelessConfigurationMgr::GetCredentialKey(int ssid, int cred)
{
	std::string res = CREDS_NS;
	res.append("/");
	res.append(std::to_string(ssid));
	res.append("/");
	res.append(std::to_string(cred));
	return res;
}

bool WirelessConfigurationMgr::EraseCredential(int ssid, int cred)
{
	bool res = true;

	if (cred >= 0 && cred < sizeof(CredentialsInfo) / sizeof(uint32_t))
	{
		res = DeleteKV(GetCredentialKey(ssid, cred));
	}
	else if (cred < 0)
	{
		for(int cred = sizeof(CredentialsInfo) / sizeof(uint32_t) - 1; res && cred >= 0; cred--)
		{
			res = DeleteKV(GetCredentialKey(ssid, cred));
		}
	}
	else
	{
		res = false;
	}

	return res;
}

bool WirelessConfigurationMgr::ResetIfCredentialsLoaded(int ssid)
{
	bool res = false;

	uint32_t loadedSsid = 0;
	res = GetKV(GetScratchKey(LOADED_SSID_ID), &loadedSsid, sizeof(loadedSsid));

	if (res) // loadedSsid value has to be valid
	{
		if (loadedSsid == ssid) // if the ssid in question is not loaded, do nothing
		{
			loadedSsid = 0;
			res = SetKV(GetScratchKey(LOADED_SSID_ID), &loadedSsid, sizeof(loadedSsid));
		}
	}

	return res;
}

bool WirelessConfigurationMgr::IsSsidBlank(const WirelessConfigurationData& data)
{
	return (data.ssid[0] == 0xFF);
}

int WirelessConfigurationMgr::FindEmptySsidEntry()
{
	for (int i = MaxRememberedNetworks; i >= 0; i--)
	{
		WirelessConfigurationData d;
		if (GetSsid(i, d) && IsSsidBlank(d)
			&& (!pendingSsid || pendingSsid->ssid != i)
		)
		{
			return i;
		}
	}

	return -1;
}