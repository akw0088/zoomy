# zoomy

I thought to myself during this pandemic, I bet I could make zoom, currently have 640x480 webcam video streaming TCP point
to point using old style video for windows capture, will add opus codec for voice, maybe throw huffman compression on the
currently uncompressed bitmap stream to save bandwidth. 640x480x4 / 1024 = 1200kb per frame,
figure that would saturate bandwidth pretty quick. But I'm sure some cams can spit out h.264 or at least jpegs for MJPEG, but will
first check out low effort compression for "good enough" operation.

Another shortcoming would be open ports and firewalls, I'm sure zoom has dedicated servers or something, but I dont
really want to go through that much trouble past basic functionality

Using the same principals it shouldn't be hard to make a remote desktop type app using the same bitmap streaming.
So I was thinking afterward make a copy called roomy or something for remote desktop type remote control, maybe just add
mouse support and force on screen keyboard usage if people really want to type things. Could also make the server side
hidden making it sort of a payload or something to spook your friends. Or add key logging and all kinds of stuff that
would probably get it marked as spyware or something
