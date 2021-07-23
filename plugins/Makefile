
all:
	$(MAKE) -C thirdparty
	$(MAKE) -C sources
	$(MAKE) -C formats
	$(MAKE) -C outputs
#	$(MAKE) -C services
#	$(MAKE) -C device

.PHONY: clean
clean:
	$(MAKE) -C thirdparty clean
	$(MAKE) -C sources clean
	$(MAKE) -C formats clean
	$(MAKE) -C outputs clean
#	$(MAKE) -C services clean
#	$(MAKE) -C device clean

.PHONY: install
install:
	$(MAKE) -C thirdparty install
	$(MAKE) -C sources install
	$(MAKE) -C formats install
	$(MAKE) -C outputs install
#	$(MAKE) -C services install
#	$(MAKE) -C device install