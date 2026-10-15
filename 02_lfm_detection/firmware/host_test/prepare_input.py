"""
Exports signal_all(1) (hydrophone channel 1, matching num=1 in lfm2_2.m) from
signal_all_35_50.mat into raw binary files the C host-test harness can read
without needing MATLAB or a .mat parser in C.

Run once:  python prepare_input.py
Produces (next to this script):
  ch1_sig.u16  - raw uint16 ADC samples, little-endian, one after another
  ch1_syn.u8   - raw uint8 sync-flag samples, little-endian, one after another
"""
import os
import numpy as np
import scipy.io as sio

HERE = os.path.dirname(os.path.abspath(__file__))
MAT_PATH = os.path.join(HERE, "..", "..", "signal_all_35_50.mat")

d = sio.loadmat(MAT_PATH, struct_as_record=False, squeeze_me=True)
sa = d["signal_all"]
ch = sa[0]  # num=1 in MATLAB (1-based) -> index 0 here

sig = np.asarray(ch.sig, dtype="<u2")
syn = np.asarray(ch.syn, dtype="<u1")

sig_path = os.path.join(HERE, "ch1_sig.u16")
syn_path = os.path.join(HERE, "ch1_syn.u8")
sig.tofile(sig_path)
syn.tofile(syn_path)

print(f"sig: {sig.size} samples -> {sig_path}")
print(f"syn: {syn.size} samples -> {syn_path}")
