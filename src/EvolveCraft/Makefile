# Wrapper Makefile — forwards all targets to the repo-root Makefile with
# GAME=evolvecraft, so you can `cd src/EvolveCraft && make game` without
# having to pass GAME= each time.

ROOT := $(CURDIR)/../..
GAME := evolvecraft

TARGETS := game build configure clean server client stop profiler \
           test_e2e test-dog test-villager killservers proxy \
           web web-build web-configure web-clean

.PHONY: $(TARGETS) help
.DEFAULT_GOAL := game

$(TARGETS):
	@$(MAKE) -C $(ROOT) $@ GAME=$(GAME)

help:
	@echo "EvolveCraft wrapper — forwards targets to $(ROOT)/Makefile with GAME=$(GAME)"
	@echo "Targets: $(TARGETS)"
