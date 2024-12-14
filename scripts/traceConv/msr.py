import os
import sys
from const import *
from collections import defaultdict
import subprocess
import time
from math import ceil

CURRFILE_PATH = os.path.dirname(os.path.abspath(__file__))
BASEPATH = os.path.join(CURRFILE_PATH, "..", "..")
sys.path.append(BASEPATH)

######### trace format #########
# the trace has sector size 512 bytes
# Timestamp(0.1us),Hostname,DiskNumber,Type,Offset,Size,ResponseTime
# 128166386787582087,mds,1,Read,1211904,4608,1331
# 128166386816018591,mds,1,Write,3216997888,16384,2326
#
###############################


# this is used to convert requests to multiple 4K blocks
BLOCK_SIZE = 4096 * 16


def preprocess(ifilepath, ofilepath=None):
    """preprocess the trace into a csv format with only necessary information
    this step aims to normalize the trace format before converting it to lcs format

    """
    start_time = time.time()

    if os.path.exists(ifilepath + ".stat"):
        return

    if not ofilepath:
        ofilepath = ifilepath + ".pre_lcs"
    ifile = open(ifilepath, "r")
    ofile = open(ofilepath, "w")
    n_req, n_original_req, n_byte = 0, 0, 0
    start_ts, end_ts = None, None
    n_read, n_write, n_delete = 0, 0, 0
    block_cnt = defaultdict(int)
    seen_blocks = set()

    for line in ifile:
        parts = line.strip().split(",")
        if len(parts) != 7:
            continue

        # Timestamp(0.1us),Hostname,DiskNumber,Type,Offset,Size,ResponseTime
        ts, host, disk, op, offset, req_size, rt = parts
        ts = int(ts) / 10000000
        if not start_ts:
            start_ts = ts
        end_ts = ts
        n_original_req += 1

        lba = int(offset)
        req_size = int(req_size)
        if op.lower() == "read":
            n_read += 1
        elif op.lower() == "write":
            n_write += 1
        elif op.lower() == "delete":
            n_delete += 1
        else:
            print("Unknown operation: {}".format(op))

        # align lba to block size to BLOCK_SIZE
        lba = lba - (lba % BLOCK_SIZE)

        for i in range(int(ceil(req_size / BLOCK_SIZE))):
            ofile.write(
                "{},{},{},{}\n".format(
                    ts - start_ts, lba + i * BLOCK_SIZE, BLOCK_SIZE, op
                )
            )

            block_cnt[lba + i * BLOCK_SIZE] += 1
            seen_blocks.add(lba + i * BLOCK_SIZE)
            n_req += 1
            n_byte += BLOCK_SIZE

    ifile.close()
    ofile.close()

    with open(ofilepath.replace(".pre_lcs", ".stat"), "w") as f:
        f.write(ifilepath + "\n")
        f.write("n_original_req: {}\n".format(n_original_req))
        f.write("n_req:          {}\n".format(n_req))
        f.write("n_obj:          {}\n".format(len(block_cnt)))
        f.write("n_byte:         {}\n".format(n_byte))
        f.write("n_uniq_byte:    {}\n".format(len(block_cnt) * BLOCK_SIZE))
        f.write("n_read:         {}\n".format(n_read))
        f.write("n_write:        {}\n".format(n_write))
        f.write("n_delete:       {}\n".format(n_delete))
        f.write("start_ts:       {}\n".format(start_ts))
        f.write("end_ts:         {}\n".format(end_ts))
        f.write("duration:       {}\n".format(end_ts - start_ts))

    print(open(ofilepath.replace(".pre_lcs", ".stat"), "r").read().strip("\n"))
    print(f"Preprocessed trace is saved to {ofilepath}")


def convert(traceConv_path, ifilepath, ofilepath=None):
    if not ofilepath:
        ofilepath = ifilepath.replace(".pre_lcs", ".lcs")
    p = subprocess.run(
        f'{traceConv_path} {ifilepath} csv -t "time-col=1,obj-id-col=2,obj-size-col=3,op-col=4,obj-id-is-num=1" -o {ofilepath} --output-format lcs_v2',
        shell=True,
    )
    if p.returncode == 0:
        print(f"Converted trace is saved to {ofilepath}")


if __name__ == "__main__":
    from utils import post_process
    DEFAULT_TRACECONV_PATH = BASEPATH + "/_build/bin/traceConv"

    if len(sys.argv) < 2:
        print("Usage: {} <trace file>".format(sys.argv[0]))
        sys.exit(1)

    ifilepath = sys.argv[1]
    traceConv_path = os.environ.get("TRACECONV_PATH", DEFAULT_TRACECONV_PATH)

    try:
        preprocess(ifilepath, ifilepath + ".pre_lcs")
        convert(traceConv_path, ifilepath + ".pre_lcs", ofilepath=ifilepath + ".lcs")
        post_process(ifilepath)
    except Exception as e:
        print(e)
        with open(ifilepath + ".fail", "w") as f:
            f.write(str(e))
