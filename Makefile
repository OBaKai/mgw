export PROJECT_ROOT_PATH := $(shell pwd)
# export PLATFORM := arm-hisiv600-linux-
#$(info $(PROJECT_ROOT_PATH))

all: core plugin msg app

core:
	$(MAKE)	-C	mgw-core
core_clean:
	$(MAKE)	-C	mgw-core clean
core_install:
	$(MAKE)	-C	mgw-core install

plugin:
	$(MAKE) -C	plugins all
plugin_clean:
	$(MAKE) -C	plugins clean
plugin_install:
	$(MAKE) -C	plugins install

msg:
	$(MAKE) -C	message
msg_clean:
	$(MAKE) -C	message clean
msg_install:
	$(MAKE) -C	message install

app:
	$(MAKE) -C	mgw-app
app_clean:
	$(MAKE) -C	mgw-app clean
app_install:
	$(MAKE) -C	mgw-app install

test:
	$(MAKE) -C	test
test_clean:
	$(MAKE) -C	test
test_install:
	$(MAKE) -C	test

.PHONY: clean
clean: core_clean plugin_clean msg_clean app_clean

.PHONY: install
install: core_install plugin_install msg_install app_install