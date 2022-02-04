# Small script that reads flasher_args.json to create a single
# flashable image that can be used for 'M997 S1'.

import json
import argparse
import os
import subprocess

argparser = argparse.ArgumentParser()
argparser.add_argument("build_dir", type=str)
argparser.add_argument("output", type=str)

args = argparser.parse_args()

flasher_args = json.load(open(os.path.join(args.build_dir, "flasher_args.json"), "r"))

partition_table = flasher_args["partition_table"]
app = flasher_args["app"]
bootloader = flasher_args["bootloader"]

bins = [partition_table, app, bootloader]
bins.sort(key=lambda b: int(b["offset"], base=16))

with open(args.output, 'wb') as imgfile:
    pos = 0
    img = b''

    for b in bins:
        fill = int(b["offset"], base=16) - pos
        img += b'\xFF' * fill
        bfile = open(os.path.join(args.build_dir, b["file"]), "rb")
        bcontent = bfile.read()
        img += bcontent
        pos += fill + len(bcontent)

    imgfile.write(img)