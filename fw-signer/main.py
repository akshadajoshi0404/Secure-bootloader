import sys
import os
import subprocess
import struct

#import ctypes //for byte array manipulation and C-like structures if needed

BOOTLOADER_SIZE = 0x8000
FWINFO_OFFSET = 0x01B0
AES_BLOCK_SIZE = 16

FWINFO_VERSION_OFFSET = 8
FWINFO_LENGTH_OFFSET = 12
FWINFO_SIGNATURE_OFFSET = FWINFO_OFFSET + AES_BLOCK_SIZE #the signature is located immediately after the firmware info section, which is the first block to be encrypted and included in the signature calculation


signing_image_filename = "fw_to_be_signed.bin"
signature_output_filename = "encrypted_signed_fw.bin"
#signing program
#    - Create a temporary firmware image that removes bootloader and zeros out the signature section
#    - Take the firmware info section and the first block(s) to be encrypted.This section contains the length, and 
#    - Use AES-CBC with the seroed IV to encrypt every block of the firmware
#        - The first block to be encrypted should include the firmware info section, so that the length and other metadata are protected by the signature
#        - The final block is the signature
#        - this is the CBC-MAC of the firmware, which is calculated by encrypting the firmware in CBC mode and taking the final block as the signature
#            - openssl enc -aes-128-cbc -nsalt -K <key> -iv <iv> -in <input_file> -out <output_file>
#        - Write the signature back to the original firmware binary, accounting for the padded length.
        

if len(sys.argv) < 3:
    print("Usage: python main.py <firmware_binary_path> <version number hex>")
    sys.exit(1)


with open(sys.argv[1], "rb") as f:
    f.seek(BOOTLOADER_SIZE) #skip the bootloader section
    fw_image = bytearray(f.read()) #bytearray?
    f.close()

#fw_info_section = fw_image[FWINFO_OFFSET:FWINFO_OFFSET + AES_BLOCK_SIZE] #the firmware info section is the first block to be encrypted, so it is included in the signature calculation
##b'\xde\xc0\xad\xdeB\x00\x00\x00\xff\xff\xff\xff\xff\xff\xff\xff'
#fw_vector_table_section = fw_image[:FWINFO_OFFSET] 
#fw = fw_image[FWINFO_OFFSET + AES_BLOCK_SIZE*2:] #the firmware image to be encrypted starts after the firmware info section, which is the first block to be encrypted
#signing_image = fw_info_section + fw_vector_table_section + fw  #the signing image is the vector table + firmware info section + zeroed signature section + the rest of the firmware

version_hex = sys.argv[2]
version_value = int(version_hex, 16)
struct.pack_into("<I", fw_image, FWINFO_OFFSET + FWINFO_VERSION_OFFSET, version_value) #update the firmware version in the firmware info section
struct.pack_into("<I", fw_image, FWINFO_OFFSET + FWINFO_LENGTH_OFFSET, len(fw_image) ) #- FWINFO_OFFSET* update the firmware length in the firmware info section, which is the length of the firmware image to be validated, starting from the vector table. This length is used by the bootloader to know how many bytes to read and validate before jumping to the main application.

signing_image = fw_image[FWINFO_OFFSET:FWINFO_OFFSET + AES_BLOCK_SIZE] 
signing_image += fw_image[:FWINFO_OFFSET]  
signing_image += fw_image[FWINFO_OFFSET + AES_BLOCK_SIZE*2:]


with open(signing_image_filename, "wb") as f:
    f.write(signing_image)
    f.close()

#call openssl to encrypt the signing image and produce the signature
key = "00112233445566778899aabbccddeeff" #example key
zeroed_iv = "00000000000000000000000000000000" #zero IV for CBC-MAC

openssl_command = f"openssl enc -aes-128-cbc -nosalt -K {key} -iv {zeroed_iv} -in {signing_image_filename} -out encrypted_signed_fw.bin"
subprocess.run(openssl_command, shell=True)

with open("encrypted_signed_fw.bin", "rb") as f:
    encrypted_fw = f.read()
    f.close()

#the signature is the last block of the encrypted firmware, which is the CBC-MAC of the firmware
signature = encrypted_fw[-AES_BLOCK_SIZE:]
signature_text = signature.hex()   


print(f"Signed firmware Version: {sys.argv[2]}")
print(f"Key = {key}")
print(f"Signature = {signature_text}")
print(f"Signature (hex) = {signature_text}")
print(f"legth of the firmware to be validated (starting from the vector table) = {len(fw_image) - FWINFO_OFFSET} bytes")

os.remove(signing_image_filename) #clean up the temporary signing image file
os.remove("encrypted_signed_fw.bin") #clean up the temporary encrypted firmware file

fw_image[FWINFO_SIGNATURE_OFFSET:FWINFO_SIGNATURE_OFFSET + AES_BLOCK_SIZE] = signature #write the signature back to the firmware image, accounting for the padded length

with open(signature_output_filename, "wb") as f:
    f.write(fw_image)
    f.close()

