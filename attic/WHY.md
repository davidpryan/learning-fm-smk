# Why the Open-SDK version was set aside

The first implementation built the learn layer into M-VAVE's official open
firmware (fm1_open V14, smk37_open V01). It compiled with JieLi's clang,
linked, and passed every vendor post-link check, but could not be packaged:
M-VAVE's Open packager reserves a 524 KB USER partition for its sample bank
and projects, leaving 453,921 bytes for the application, and the Open app
plus any learn code is larger than that (the first Stage 1 image was 21 KB
over at -O2; -Oz was not enough). The stock SMK-37 flash map, which SLOOP
and every genkigenki port use, gives the app 619,520 bytes.

The code here still compiles and passes its host tests against the vendor
browser engine (attic/tools/host_build.sh) and documents the engine API,
SysEx vocabulary and stage behaviour that the SLOOP-based layer re-targets.
