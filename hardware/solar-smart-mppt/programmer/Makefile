# host tests of the SWD / flash / image code (the C3 firmware itself builds with PlatformIO)
FW = ../firmware
test: build/test_swd
	build/test_swd $(FW)/build-pa10/smppt.bin $(FW)/build-pa10/smppt.hex

build/test_swd: test/test_swd.cpp lib/mspm0swd/*.cpp lib/mspm0swd/*.h
	@mkdir -p build
	g++ -std=c++17 -Wall -Wextra -fsanitize=address,undefined -g test/test_swd.cpp lib/mspm0swd/Swd.cpp \
	    lib/mspm0swd/Mspm0Flash.cpp lib/mspm0swd/Image.cpp -o $@

.PHONY: test
