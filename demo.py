from HWDecode import get_gop_frame, get_num_gops, get_gop_frames, load_gop_frames
import time
import os

num_frames = get_num_gops("1000074905.mp4")
print("num_frames: ", num_frames)

num_segments = 4
loaded_frames = load_gop_frames("1000074905.mp4", num_segments)
print("shape of loaded_frames", loaded_frames.shape)
