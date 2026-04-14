# Wrapper Makefile — forwards all targets to the repo-root Makefile with
# GAME=cellcraft, so you can `cd src/CellCraft && make game` without
# having to pass GAME= each time.

ROOT := $(CURDIR)/../..
GAME := cellcraft

TARGETS := game build configure clean server client stop \
           cellcraft-build cellcraft-godot

.PHONY: $(TARGETS) help
.DEFAULT_GOAL := game

$(TARGETS):
	@$(MAKE) -C $(ROOT) $@ GAME=$(GAME)

help:
	@echo "CellCraft wrapper — forwards targets to $(ROOT)/Makefile with GAME=$(GAME)"
	@echo "Targets: $(TARGETS)"
	@echo ""
	@echo "  make game           Launch the drawing MVP"
	@echo "  make game DEMO=1    Launch with pre-populated demo strokes"
	@echo "  make cellcraft-godot  Run the legacy Godot prototype (visual reference)"
	@echo "  make stop           Kill all cellcraft processes"
