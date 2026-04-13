# Wrapper Makefile — forwards all targets to the repo-root Makefile with
# GAME=modcraft, so you can `cd src/ModCraft && make game` without having
# to pass GAME= each time.

ROOT := $(CURDIR)/../..
GAME := modcraft

TARGETS := game build configure clean server client stop profiler \
           test_e2e test-dog test-villager killservers proxy \
           web web-build web-configure web-clean

.PHONY: $(TARGETS) help
.DEFAULT_GOAL := game

$(TARGETS):
	@$(MAKE) -C $(ROOT) $@ GAME=$(GAME)

help:
	@echo "ModCraft wrapper — forwards targets to $(ROOT)/Makefile with GAME=$(GAME)"
	@echo "Targets: $(TARGETS)"
