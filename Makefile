
default:
	pio run

flash:
	pio run -t upload

monitor:
	pio device monitor
