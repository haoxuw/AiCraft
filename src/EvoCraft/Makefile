# EvoCraft Makefile forwarder — cd into src/EvoCraft/ and `make <target>`
# invokes the root Makefile with GAME=evocraft preset.

ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)

.PHONY: game server client stop test_e2e build configure clean
game server client stop test_e2e build configure clean:
	$(MAKE) -C $(ROOT) $@ GAME=evocraft $(filter-out $@,$(MAKECMDGOALS))

%:
	@:
