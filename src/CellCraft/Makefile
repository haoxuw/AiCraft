# CellCraft wrapper — CellCraft is a standalone Three.js + TypeScript web
# client at ./web/. Nothing native; nothing shared with src/platform/ or
# CivCraft. Every target here delegates to web/ via npm scripts.

.PHONY: web dev build preview test test-watch install clean stop help
.DEFAULT_GOAL := web

web dev:
	@cd web && npm run dev

build:
	@cd web && npm run build

preview: build
	@cd web && npm run preview

test:
	@cd web && npm test

test-watch:
	@cd web && npx vitest

install:
	@cd web && npm install

clean:
	@rm -rf web/dist web/node_modules/.vite

stop:
	@-pkill -f "vite.*cellcraft/web" 2>/dev/null; true

help:
	@echo "CellCraft — Three.js/TypeScript web client in ./web/"
	@echo ""
	@echo "  make web           Vite dev server (default)"
	@echo "  make build         Production build"
	@echo "  make preview       Serve the production build"
	@echo "  make test          Run vitest once"
	@echo "  make test-watch    Run vitest in watch mode"
	@echo "  make install       npm install"
	@echo "  make clean         Remove dist + vite cache"
