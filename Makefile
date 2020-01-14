all:
	west build

flash:
	west build
	west sign -t imgtool -- --key n2_fota.pem
	west flash --hex-file build/zephyr/zephyr.signed.hex