all:
	@~/venv/bin/platformio run -e pocketwroom
upload:
	@~/venv/bin/platformio run -e pocketwroom -t upload -t monitor
