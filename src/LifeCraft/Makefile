# Wrapper Makefile — forwards all targets to the repo-root Makefile with
# GAME=lifecraft, so you can `cd src/LifeCraft && make game` without
# having to pass GAME= each time.

ROOT := $(CURDIR)/../..
GAME := lifecraft

TARGETS := game build configure clean server client stop \
           lifecraft-build lifecraft-godot

.PHONY: $(TARGETS) help
.DEFAULT_GOAL := game

$(TARGETS):
	@$(MAKE) -C $(ROOT) $@ GAME=$(GAME)

help:
	@echo "LifeCraft wrapper — forwards targets to $(ROOT)/Makefile with GAME=$(GAME)"
	@echo "Targets: $(TARGETS)"
	@echo ""
	@echo "  make game           Launch the drawing MVP"
	@echo "  make game DEMO=1    Launch with pre-populated demo strokes"
	@echo "  make lifecraft-godot  Run the legacy Godot prototype (visual reference)"
	@echo "  make stop           Kill all lifecraft processes"
