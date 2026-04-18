Setting up the project structure and Makefile for the bootloader. The Makefile is configured to compile the bootloader source code and link it to create the final binary. The source files are organized in a way that allows for easy management and scalability as the project grows.

### Bootloader 
    1. Create a directory structure for the bootloader:
    - bootloader/
        - src/
        - bootloader.c
        - core/
            - system.c
            - timer.c
        - inc/
        - bootloader.h
        - core/
            - system.h
            - timer.h
        - Makefile

    2. The Makefile is set up to compile the bootloader source code and link it to create the final binary. It includes paths to the source and include directories, as well as the necessary flags for compilation.

    3. The Makefile defines the target binary as "bootloader" and specifies the source files needed for compilation. 

    4. Prevent linking when bootloader is larger then the maximum allowed size.

    5. Pad the bootloader binary to the maximum allowed size with 0xFF bytes.We are doing this to ensure that the bootloader occupies the entire reserved space in memory, which can help prevent issues with memory fragmentation and ensure that the bootloader is properly aligned in memory for optimal performance. Additionally, padding the binary can help to prevent accidental overwriting of important data or code that may be located adjacent to the bootloader in memory.The compiled boot file then can be used in conjugation with the firmware binary to create a complete image that can be flashed onto the target device.


### Application Firmware
    1. Create a directory structure for the application firmware:
    - firmware/
        - src/
        - main.c
        - core/
            - system.c
            - timer.c
        - inc/
        - main.h
        - core/
            - system.h
            - timer.h
        - Makefile

        2. Include the bootloader binary by wrapping the raw data up inside an object file. 


///////////////////////////////
The libopencm3 build completed successfully. All targets compiled and archived without errors.

Summary: The issue was that git submodule update --remote --merge moved the submodule from a pinned commit (189017b2) to the latest origin/master (87a080c9), which requires Python to generate header files at build time. The Windows Store python3 stub was intercepting the call. Prepending bin to PATH resolved it.

I also added a build_libopencm3 task to your tasks.json so you can rebuild it from VS Code in the future without needing to manually set the PATH each time.
//////////////////////////////////////