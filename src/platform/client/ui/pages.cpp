#include "client/ui/pages.h"
#include "client/ui/components.h"
#include "client/ui/action_router.h"
#include "client/ui/action_payloads.h"
#include "client/game_vk.h"
#include "client/cef_browser_host.h"
#include "client/save_browser.h"
#include "agent/agent_client.h"
#include "logic/artifact_registry.h"
#include "logic/world_templates.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace solarium::ui {

// ─────────────────────────────────────────────────────────────────────────
// Page helpers — every page is assembled as a complete `<html>` document,
// written to /tmp/solarium_page.html, and loaded via a `file://` URL. The
// bulk CSS lives in real .css files (src/resources/ui/components.css and
// dex.css) — only the per-theme `:root{...}` block + per-page extras stay
// inline. No more `data:` URL escape tax: bare `#`, bare `%`.
// ─────────────────────────────────────────────────────────────────────────
namespace {

std::string pageHead(const vk::Game& game,
                     const std::string& extraCss = "") {
	return "<!doctype html><html><head>"
	       "<link rel=\"stylesheet\" href=\""
	     + componentsCssUrl(game.execDir())
	     + "\"><style>"
	     + themeRoot(game.settings().theme_id) + extraCss
	     + "</style></head><body>";
}

// Same as pageHead but pulls the Pokédex/Civilopedia chrome stylesheet
// (sidebar + detail card) instead of components.css. Used by Handbook +
// Character Select.
std::string pageHeadDex(const vk::Game& game,
                        const std::string& extraCss = "") {
	return "<!doctype html><html><head>"
	       "<link rel=\"stylesheet\" href=\""
	     + dexCssUrl(game.execDir())
	     + "\"><style>"
	     + themeRoot(game.settings().theme_id) + extraCss
	     + "</style></head><body>";
}

std::string pageTail() {
	return versionTag() + pageJs() + "</body></html>";
}

// Write assembled HTML to a per-call unique file under /tmp/ and return
// the `file://` URL. PageCache builds the 8 static pages back-to-back at
// boot, so a SINGLE rolling file would have every cached URL point at
// whichever wrote last (prior bug — every cached URL ended up showing the
// lobby because lobby was the last builder PageCache called). Per-call
// unique paths fix that, and disk usage is trivial (each page ~3 KB).
std::string finalizeFileUrl(const std::string& html) {
	static int counter = 0;
	std::string path = "/tmp/solarium_page_" +
	                   std::to_string(++counter) + ".html";
	std::ofstream f(path, std::ios::binary | std::ios::trunc);
	f << html;
	f.close();
	// Stable alias for test rigs (smoke test greps this path). CEF still
	// loads the unique file:// URL above so its cache key stays per-page.
	std::ofstream g("/tmp/solarium_page.html", std::ios::binary | std::ios::trunc);
	g << html;
	g.close();
	return "file://" + path;
}

} // namespace

std::string mainPage(const vk::Game& game) {
	return finalizeFileUrl(pageHead(game)
	     + "<h1>Solarium</h1>"
	       "<div class='tag'>A voxel sandbox civilization</div>"
	       "<button class='btn' onclick=\"send('singleplayer')\">Singleplayer</button>"
	       "<button class='btn' onclick=\"send('multiplayer')\">Multiplayer</button>"
	       "<button class='btn' onclick=\"send('handbook')\">Handbook</button>"
	       "<button class='btn' onclick=\"send('mods')\">Mods</button>"
	       "<button class='btn' onclick=\"send('settings')\">Settings</button>"
	       "<button class='btn' onclick=\"send('quit')\">Quit</button>"
	     + pageTail());
}

std::string pausePage(const vk::Game& game) {
	// Centred + dark scrim radial so the running world dims behind it.
	const std::string css =
		"body{justify-content:center;align-items:center;"
		"background:radial-gradient(ellipse at center,"
		"rgba(10,5,5,0.65) 0%,rgba(10,5,5,0.25) 60%)}"
		"h1{font-size:64px;letter-spacing:8px;margin:0 0 24px}"
		".btn{width:280px}";
	return finalizeFileUrl(pageHead(game, css)
	     + "<h1>Paused</h1>"
	       "<button class='btn' onclick=\"send('resume')\">Resume</button>"
	       "<button class='btn' onclick=\"send('settings')\">Settings</button>"
	       "<button class='btn' onclick=\"send('main_menu')\">Main Menu</button>"
	       "<button class='btn' onclick=\"send('quit')\">Quit</button>"
	     + pageTail());
}

std::string deathPage(const vk::Game& game) {
	// Red-tinted radial scrim + crimson title for "you died" drama.
	// %23 = '#' so the data: URL parser doesn't fragment the payload.
	const std::string css =
		"body{justify-content:center;align-items:center;"
		"background:radial-gradient(ellipse at center,"
		"rgba(80,10,10,0.55) 0%,rgba(20,5,5,0.30) 60%)}"
		"h1{font-size:72px;letter-spacing:10px;margin:0 0 14px;"
		"color:%23e85a4a;text-shadow:0 4px 18px rgba(0,0,0,0.85),"
		"0 0 24px rgba(232,90,74,0.45)}"
		".tag{margin:0 0 32px;color:%23e85a4a;opacity:0.85}"
		".btn{width:280px}";
	return finalizeFileUrl(pageHead(game, css)
	     + "<h1>You Died</h1>"
	       "<div class='tag'>Pick yourself up, traveller</div>"
	       "<button class='btn' onclick=\"send('respawn')\">Respawn</button>"
	       "<button class='btn' onclick=\"send('main_menu')\">Main Menu</button>"
	     + pageTail());
}

std::string multiplayerHubPage(const vk::Game& game) {
	const std::string css =
		"h1{font-size:48px;letter-spacing:6px;margin:0 0 4px}"
		".tag{margin:0 0 28px}"
		".btn{width:340px}"
		".btn small{display:block;font-size:12px;opacity:0.65;"
		"margin-top:6px;letter-spacing:1px}";
	return finalizeFileUrl(pageHead(game, css)
	     + "<h1>Multiplayer</h1>"
	       "<div class='tag'>Host a session, or join one on your network</div>"
	       "<button class='btn' onclick=\"send('mp_host')\">"
	       "Host New Game<small>visible to LAN players</small></button>"
	       "<button class='btn' onclick=\"send('mp_join')\">"
	       "Join LAN Game<small>browse servers on UDP 7778</small></button>"
	       "<button class='btn back' onclick=\"send('back')\">Back</button>"
	     + pageTail());
}

std::string multiplayerPage(const vk::Game& game) {
	// Pinned to client version so we can grey-out servers that don't
	// match. Bump alongside Settings → About / version.txt.
	constexpr const char* kClientVer = "0.2.0";
	const std::string css =
		// LAN-server row.
		".srv{display:grid;grid-template-columns:1fr auto auto auto;"
		"column-gap:18px;align-items:center;width:680px;padding:12px 22px;"
		"margin:5px 0;background:rgba(20,13,8,0.97);"
		"border:1px solid var(--accent-mid);color:var(--accent-hi);"
		"font-size:14px;letter-spacing:1px;font-family:monospace;"
		"cursor:pointer;transition:background 0.15s,transform 0.15s}"
		".srv.mismatch{opacity:0.55;border-color:rgba(184,136,56,0.4)}"
		".srv:hover{background:rgba(94,67,30,0.92);transform:scale(1.01)}"
		".srv .ip{color:var(--accent-hi)}"
		".srv .world{color:var(--ink);font-size:12px;"
		"text-transform:uppercase;letter-spacing:2px}"
		".srv .ver{color:var(--accent-mid);font-size:11px;letter-spacing:1px}"
		".srv .pl{color:var(--accent-mid);font-size:13px;text-align:right;"
		"min-width:90px}"
		".empty{opacity:0.5;font-style:italic;margin:24px 0}";
	std::string html = pageHead(game, css);
	html += "<h1>Multiplayer</h1><div class='tag'>";
	if (!game.lanBrowser().listening()) {
		html += "Could not bind UDP 7778 - try --host HOST --port PORT";
	} else {
		const auto& srvs = game.lanBrowser().servers();
		if (srvs.empty()) {
			html += "Scanning UDP 7778...";
		} else {
			char hdr[64];
			std::snprintf(hdr, sizeof(hdr), "%zu LAN server%s",
				srvs.size(), srvs.size() == 1 ? "" : "s");
			html += hdr;
		}
	}
	html += "</div><div class='filters'>"
	        "<label><input type='checkbox' id='fver' checked>"
	        "Hide version mismatches</label></div>";
	for (const auto& s : game.lanBrowser().servers()) {
		bool match = (s.version == kClientVer);
		char row[400];
		std::snprintf(row, sizeof(row),
			"<div class='srv%s' data-match='%d' "
			"onclick=\"send('join:%s:%d')\">"
			"<span class='ip'>%s:%d</span>"
			"<span class='world'>%s</span>"
			"<span class='ver'>v%s</span>"
			"<span class='pl'>%d player%s</span></div>",
			match ? "" : " mismatch", match ? 1 : 0,
			s.ip.c_str(), s.port,
			s.ip.c_str(), s.port,
			s.world.empty()   ? "?" : s.world.c_str(),
			s.version.empty() ? "?" : s.version.c_str(),
			s.humans, s.humans == 1 ? "" : "s");
		html += row;
	}
	html += "<button class='btn' onclick=\"send('multiplayer')\">Refresh</button>"
	        "<button class='btn back' onclick=\"send('back')\">Back</button>"
	        // Wire the version filter to hide mismatched rows on toggle.
	        "<script>"
	        "document.getElementById('fver').addEventListener('change',e=>{"
	        "const hide=e.target.checked;"
	        "document.querySelectorAll('.srv').forEach(r=>{"
	        "r.style.display=(hide && r.dataset.match==='0')?'none':'';});});"
	        "document.getElementById('fver').dispatchEvent(new Event('change'));"
	        "</script>";
	return finalizeFileUrl(html + pageTail());
}

std::string saveSlotsPage(const vk::Game& game) {
	auto saves = solarium::vk::scanSaves("saves");
	const std::string css =
		"body{justify-content:flex-start;padding:60px 60px 60px;"
		"height:auto;min-height:100vh;box-sizing:border-box;align-items:center}"
		"h1{font-size:48px;letter-spacing:6px;margin:0 0 8px}"
		".tag{margin:0 0 32px}"
		".tiles{display:grid;grid-template-columns:repeat(auto-fill,"
		"minmax(320px,1fr));gap:16px;width:100%;max-width:1100px}"
		".tile{background:rgba(20,13,8,0.97);"
		"border:1px solid var(--accent-mid);"
		"padding:22px 24px;cursor:pointer;font-family:inherit;color:inherit;"
		"text-align:left;transition:background 0.15s,transform 0.15s,"
		"box-shadow 0.15s}"
		".tile:hover{background:rgba(94,67,30,0.95);transform:translateY(-2px);"
		"box-shadow:0 6px 20px rgba(0,0,0,0.5)}"
		".tile h3{margin:0 0 6px;color:var(--accent-hi);font-size:22px;"
		"letter-spacing:2px;font-weight:400}"
		".tile .meta{font-size:12px;color:var(--accent-mid);"
		"font-family:monospace;letter-spacing:1px;margin-bottom:6px;"
		"display:block;opacity:0.7}"
		".tile .when{font-size:11px;color:var(--accent-mid);opacity:0.6}"
		".tile.new{background:rgba(94,67,30,0.88);"
		"border:2px dashed var(--accent-mid)}"
		".tile.new h3{color:var(--accent-hi)}"
		".tile.save{position:relative}"
		// Brass-toned destructive button — rust-red only on hover.
		".tile .del{position:absolute;top:8px;right:8px;"
		"padding:4px 10px;font-size:10px;letter-spacing:1px;"
		"background:rgba(20,13,8,0.92);color:var(--accent-mid);"
		"border:1px solid var(--accent-mid);font-family:inherit;"
		"cursor:pointer;text-transform:uppercase;opacity:0.45;"
		"transition:opacity 0.15s,background 0.15s,color 0.15s}"
		".tile.save:hover .del{opacity:1}"
		".tile .del:hover{background:rgba(96,28,18,0.95);"
		"color:%23f0d8b8;border-color:%23a04030}"
		".back{margin-top:24px;width:160px}";
	std::string html = pageHead(game, css)
	     + "<h1>Choose Save</h1>"
	       "<div class='tag'>Continue an old world or start fresh</div>"
	       "<div class='tiles'>"
	       "<button class='tile new' onclick=\"send('new_world')\">"
	       "<h3>New World</h3>"
	       "<span class='meta'>pick a template + seed</span></button>";
	for (const auto& s : saves) {
		// Two click targets per tile — outer loads the save, inner ×
		// deletes (with confirm). stopPropagation in the inner onclick
		// prevents the outer load from firing alongside.
		html += "<div class='tile save' onclick=\"send('load:" + s.path + "')\">"
		        "<button class='del' "
		        "onclick=\"event.stopPropagation();"
		        "if(confirm('Delete &quot;" + enc(compact(s.name, 40)) +
		        "&quot;?'))send('delete_save:" + s.path + "')\">"
		        "Delete</button>"
		        "<h3>" + enc(compact(s.name, 80)) + "</h3>"
		        "<span class='meta'>" + enc(compact(s.templateName, 60)) + "</span>"
		        "<span class='when'>" + enc(compact(s.lastPlayed, 40)) + "</span>"
		        "</div>";
	}
	html += "</div>"
	        "<button class='btn back' onclick=\"send('back')\">Back</button>";
	return finalizeFileUrl(html + pageTail());
}

std::string worldPickerPage(const vk::Game& game) {
	const std::string css =
		"body{justify-content:flex-start;padding:60px 60px 60px;height:auto;"
		"min-height:100vh;box-sizing:border-box;align-items:center}"
		"h1{font-size:48px;letter-spacing:6px;margin:0 0 8px}"
		".tag{margin:0 0 32px}"
		".tiles{display:grid;grid-template-columns:repeat(auto-fill,"
		"minmax(320px,1fr));gap:16px;width:100%;max-width:1100px}"
		".tile{background:rgba(20,13,8,0.97);"
		"border:1px solid var(--accent-mid);"
		"padding:22px 24px;cursor:pointer;font-family:inherit;color:inherit;"
		"text-align:left;transition:background 0.15s,transform 0.15s,"
		"box-shadow 0.15s}"
		".tile:hover{background:rgba(94,67,30,0.95);transform:translateY(-2px);"
		"box-shadow:0 6px 20px rgba(0,0,0,0.5)}"
		".tile h3{margin:0 0 6px;color:var(--accent-hi);font-size:22px;"
		"letter-spacing:2px;font-weight:400}"
		".tile .id{font-size:11px;color:var(--accent-mid);"
		"font-family:monospace;letter-spacing:1px;margin-bottom:8px;"
		"display:block;opacity:0.7}"
		".tile p{margin:0;font-size:13px;line-height:1.45;opacity:0.88}"
		// Bottom config strip — Name / Seed / Villagers. Same scrim/border
		// treatment as a row tile so it reads as part of the same chrome.
		".opts{display:flex;gap:18px;align-items:center;margin-top:28px;"
		"padding:14px 22px;background:rgba(20,13,8,0.97);"
		"border:1px solid rgba(184,136,56,0.5)}"
		".opts label{font-size:12px;letter-spacing:2px;color:var(--accent-mid);"
		"text-transform:uppercase;display:flex;align-items:center;gap:10px}"
		".opts input{background:rgba(40,30,20,0.95);color:var(--accent-hi);"
		"border:1px solid var(--accent-mid);font-family:monospace;"
		"font-size:13px;padding:6px 8px;width:90px}"
		".opts input[type='range']{width:160px;accent-color:var(--accent-hi)}"
		".opts .val{font-family:monospace;color:var(--accent-hi);"
		"min-width:32px;text-align:right}"
		".opts button{padding:6px 12px;background:rgba(94,67,30,0.85);"
		"color:var(--accent-hi);border:1px solid var(--accent-mid);"
		"font-family:inherit;font-size:11px;cursor:pointer;letter-spacing:1px}"
		".back{margin-top:24px;width:160px}";
	std::string html = pageHead(game, css)
	     + "<h1>Choose World</h1>"
	       "<div class='tag'>Pick where your story begins</div>"
	       "<div class='tiles'>";
	for (size_t i = 0; i < solarium::kWorldTemplateCount; ++i) {
		const auto& t = solarium::kWorldTemplates[i];
		// Pull display name + description from the artifact registry
		// (the .py is the source of truth); fall back to the constexpr
		// table if nothing is registered.
		std::string name = t.fallbackName;
		std::string desc;
		if (auto* e = game.artifactRegistry().findById(t.id)) {
			if (!e->name.empty())        name = e->name;
			if (!e->description.empty()) desc = e->description;
		}
		html += "<button class='tile' onclick=\"send('world:" +
		        std::string(t.id) + "')\">"
		        "<h3>" + enc(compact(name, 80)) + "</h3>"
		        "<span class='id'>" + std::string(t.id) + "</span>"
		        "<p>" + enc(desc.empty() ? "(no description)"
		                                 : compact(desc, 220)) + "</p>"
		        "</button>";
	}
	html += "</div>"
	        "<div class='opts'>"
	        "<label>Name<input type='text' id='wname' "
	        "placeholder='My World' maxlength='32' style='width:160px'></label>"
	        "<label>Seed<input type='number' id='wseed' value='42' min='0' max='99999'></label>"
	        "<button onclick=\"document.getElementById('wseed').value="
	        "Math.floor(Math.random()*99999)\">Randomise</button>"
	        "<label>Villagers<input type='range' id='wvill' min='0' max='100' value='0'>"
	        "<span class='val' id='wvillv'>auto</span></label>"
	        "</div>"
	        "<button class='btn back' onclick=\"send('back')\">Back</button>"
	        "<script>"
	        "document.getElementById('wvill').addEventListener('input',e=>{"
	        "document.getElementById('wvillv').textContent="
	        "e.target.value==='0'?'auto':e.target.value;});"
	        // Hijack tile clicks: append seed + villagers + name to the
	        // action. URI-encode the name so ':' inside it doesn't split
	        // the action argument.
	        "document.querySelectorAll('.tile').forEach(t=>{"
	        "const id=t.getAttribute('onclick').match(/world:([\\w_]+)/)[1];"
	        "t.onclick=()=>{const s=document.getElementById('wseed').value||'42';"
	        "const v=document.getElementById('wvill').value;"
	        "const n=encodeURIComponent("
	        "document.getElementById('wname').value.trim()||'My World');"
	        "send('world:'+id+':'+s+':'+v+':'+n);};});"
	        "</script>";
	return finalizeFileUrl(html + pageTail());
}

std::string handbookPage(const vk::Game& game) {
	using Entry = const solarium::ArtifactEntry*;

	// Pick the section bucket within a top-level group based on artifact
	// fields. Falls back to subcategory or "Other".
	auto livingBucket = [](Entry e) -> const char* {
		if (e->flag("playable")) return "Heroes";
		const std::string& sc = e->subcategory;
		if (sc == "humanoid")  return "Villagers";
		if (sc == "hostile" || sc == "predator")  return "Hostile";
		if (sc == "animal" || sc == "livestock")  return "Animals";
		return "Wildlife";
	};
	auto resourceBucket = [](Entry e) -> const char* {
		const std::string& id = e->id;
		if (id.find("footstep") != std::string::npos) return "Footsteps";
		if (id.find("combat") != std::string::npos)   return "Combat";
		if (id.find("music") != std::string::npos)    return "Music";
		if (id.find("ambient") != std::string::npos)  return "Ambient";
		if (id.find("ui") != std::string::npos)       return "UI";
		if (id.find("creature") != std::string::npos) return "Creatures";
		if (id.find("block") != std::string::npos ||
		    id.find("door") != std::string::npos)     return "Blocks";
		if (id.find("explosion") != std::string::npos ||
		    id.find("spell") != std::string::npos)    return "Spells";
		return "Misc";
	};
	auto subOrOther = [](Entry e) -> const char* {
		return e->subcategory.empty() ? "Other" : e->subcategory.c_str();
	};

	using Bucketer = std::function<const char*(Entry)>;
	struct Group {
		const char*              label;
		std::vector<const char*> cats;
		Bucketer                 bucketer;
	};
	std::vector<Group> groups = {
		{"Creatures",  {"living"},                livingBucket},
		{"Items",      {"item"},                  subOrOther},
		{"Effects",    {"effect"},                subOrOther},
		{"Blocks",     {"block"},                 subOrOther},
		{"Structures", {"structure"},             subOrOther},
		{"Worlds",     {"world", "annotation"},   subOrOther},
		{"Behaviors",  {"behavior"},              subOrOther},
		{"Audio",      {"resource"},              resourceBucket},
		{"Models",     {"model"},                 subOrOther},
	};

	// Per-group items, sorted by (bucket, displayName) so buckets render
	// as contiguous blocks with alpha order inside.
	struct Item { int groupIdx; std::string bucket; Entry e; const char* cat; };
	std::vector<Item> items;
	size_t totalCount = 0;
	for (size_t gi = 0; gi < groups.size(); ++gi) {
		std::vector<Item> groupItems;
		const char* primaryCat = groups[gi].cats.empty()
		    ? "" : groups[gi].cats[0];
		for (const char* cat : groups[gi].cats) {
			for (auto* e : game.artifactRegistry().byCategory(cat)) {
				groupItems.push_back({(int)gi, groups[gi].bucketer(e), e, primaryCat});
			}
		}
		std::sort(groupItems.begin(), groupItems.end(),
			[](const Item& a, const Item& b){
				if (a.bucket != b.bucket) return a.bucket < b.bucket;
				std::string an = a.e->name.empty() ? a.e->id : a.e->name;
				std::string bn = b.e->name.empty() ? b.e->id : b.e->name;
				return an < bn;
			});
		totalCount += groupItems.size();
		for (auto& it : groupItems) items.push_back(std::move(it));
	}

	// Per-page CSS — wrapping tab strip + sub-section dividers.
	// Each group renders into a .sb-grp; only the .on group is shown.
	// Tabs sit above the search box and switch the visible group.
	const std::string css =
		".tabs{display:flex;flex-wrap:wrap;gap:4px;padding:10px 14px 12px;"
		"border-bottom:1px solid rgba(184,136,56,0.25)}"
		".tab{padding:6px 9px;font-size:10px;letter-spacing:1.5px;"
		"color:var(--accent-mid);text-transform:uppercase;cursor:pointer;"
		"border:1px solid rgba(184,136,56,0.25);border-radius:2px;"
		"background:rgba(0,0,0,0.25);user-select:none;transition:all 0.12s;"
		"font-family:inherit}"
		".tab:hover{color:var(--accent-hi);border-color:rgba(184,136,56,0.55);"
		"background:rgba(184,136,56,0.10)}"
		".tab.on{color:#0c0a08;background:var(--accent-hi);"
		"border-color:var(--accent-hi);font-weight:600;"
		"box-shadow:0 0 8px rgba(243,196,76,0.35)}"
		".tab .cnt{margin-left:5px;opacity:0.6;font-weight:400}"
		".tab.on .cnt{opacity:0.65}"
		".sb-grp{display:none}"
		".sb-grp.on{display:block}"
		".sb-sub{padding:10px 22px 4px;font-size:10px;letter-spacing:2px;"
		"color:var(--accent-mid);text-transform:uppercase;opacity:0.78;"
		"border-top:1px solid rgba(184,136,56,0.12);margin-top:6px}"
		".sb-sub:first-child{border-top:none;margin-top:0}"
		".sb-grp .sb-row{padding-left:22px}";
	std::string html = pageHeadDex(game, css)
	     + "<aside class='sidebar'>"
	       "<div class='sb-head'><h1>Handbook</h1>"
	       "<div class='sub'>" + std::to_string(totalCount) + " entries</div></div>"
	       "<div class='tabs'>";

	// Tab strip — one pill per non-empty group. First tab starts active.
	for (size_t gi = 0; gi < groups.size(); ++gi) {
		int groupCount = 0;
		for (const auto& it : items) if (it.groupIdx == (int)gi) ++groupCount;
		if (groupCount == 0) continue;
		const bool isFirst = (gi == 0);
		html += "<div class='tab" + std::string(isFirst ? " on" : "") +
		        "' data-tab='" + std::to_string(gi) +
		        "' onclick=\"switchTab(this)\">" +
		        enc(groups[gi].label) +
		        "<span class='cnt'>" + std::to_string(groupCount) +
		        "</span></div>";
	}
	html += "</div>"
	        "<input class='sb-search' id='dexq' placeholder='filter...' "
	        "oninput=\"dexfilter(this.value)\">"
	        "<div class='sb-list' id='dexlist'>";

	// Render groups in order, splitting each into buckets. Only the first
	// group (.on class) is visible at a time; switchTab() toggles them.
	for (size_t gi = 0; gi < groups.size(); ++gi) {
		int groupCount = 0;
		for (const auto& it : items) if (it.groupIdx == (int)gi) ++groupCount;
		if (groupCount == 0) continue;
		const bool isFirst = (gi == 0);
		html += "<div class='sb-grp" + std::string(isFirst ? " on" : "") +
		        "' data-grp='" + std::to_string(gi) + "'>";
		std::string lastBucket;
		for (const auto& it : items) {
			if (it.groupIdx != (int)gi) continue;
			if (it.bucket != lastBucket) {
				html += "<div class='sb-sub'>" + enc(it.bucket) + "</div>";
				lastBucket = it.bucket;
			}
			std::string display = it.e->name.empty() ? it.e->id : it.e->name;
			std::string key = std::string(it.cat) + ":" + it.e->id;
			std::string q = display + " " + it.e->id + " " + it.bucket;
			std::transform(q.begin(), q.end(), q.begin(),
				[](unsigned char c){ return std::tolower(c); });
			// data-key carries the composite ENTRIES lookup key; the bare
			// id goes into pick:<id> so the C++ preview lookup hits the
			// artifact registry's findById.
			html += "<button class='sb-row' data-key='" + enc(key) +
			        "' data-id='" + enc(it.e->id) +
			        "' data-q='" + enc(q) + "' "
			        "onclick=\"pick(this)\" onmouseenter=\"hover(this)\">" +
			        enc(display) + "<span class='id'>" +
			        enc(it.e->id) + "</span></button>";
		}
		html += "</div>";
	}
	html += "</div>"
	        "<div class='sb-foot'>"
	        "<button onclick=\"send('back')\">Back</button>"
	        "</div></aside>"
	        "<main class='detail'>"
	        "<div class='detail-card' id='card'></div></main>"
	     + versionTag();

	// ENTRIES table (cat:id → {name, cat, sub, desc, attrs}).
	html += "<script>const ENTRIES={";
	for (size_t i = 0; i < items.size(); ++i) {
		const auto& it = items[i];
		if (i) html += ",";
		std::string display = it.e->name.empty() ? it.e->id : it.e->name;
		html += "'" + std::string(it.cat) + ":" + it.e->id +
		        "':{name:'" + enc(compact(display, 80)) +
		        "',cat:'" + enc(it.cat) +
		        "',sub:'" + enc(it.bucket) +
		        "',desc:'" + enc(compact(it.e->description, 400)) +
		        "',attrs:[";
		std::vector<std::pair<std::string, std::string>> attrs;
		for (auto& kv : it.e->fields) {
			if (kv.first == "description") continue;
			if (kv.second.empty()) continue;
			attrs.emplace_back(kv.first, kv.second);
		}
		std::sort(attrs.begin(), attrs.end());
		if (!it.e->subcategory.empty() && it.e->subcategory != it.bucket)
			attrs.insert(attrs.begin(), {"subcategory", it.e->subcategory});
		for (size_t j = 0; j < attrs.size(); ++j) {
			if (j) html += ",";
			html += "['" + enc(attrs[j].first) + "','" +
			        enc(compact(attrs[j].second)) + "']";
		}
		html += "]}";
	}
	html += "};"
	        "function render(key){"
	        "const e=ENTRIES[key];if(!e)return;"
	        // Headline (badges + name) sits over the world preview.
	        "let h=\"<div class='headline'>\"+"
	        "\"<span class='badge'>\"+e.cat+\"</span>\"+"
	        "\"<span class='badge'>\"+e.sub+\"</span>\"+"
	        "\"<span class='badge'>\"+key.split(':')[1]+\"</span>\"+"
	        "\"<h2>\"+e.name+\"</h2></div>\";"
	        // Bottom panel: description + divider + stats live together
	        // inside one opaque container, so neither sits on the bare
	        // world preview.
	        "h+=\"<div class='card-body'>\";"
	        "if(e.desc)h+=\"<div class='desc'>\"+e.desc+\"</div>\";"
	        "if(e.desc&&e.attrs.length)h+=\"<hr class='div'/>\";"
	        "if(e.attrs.length){h+=\"<div class='attrs'>\";"
	        "for(const[k,v] of e.attrs)"
	        "h+=\"<span class='k'>\"+k+\"</span>"
	        "<span class='v'>\"+v+\"</span>\";"
	        "h+=\"</div>\";}"
	        // Edit button — opens Monaco-backed source editor for this artifact.
	        "h+=\"<div class='actions'>"
	        "<button class='edit-btn' onclick=\\\"send('edit:\"+key+\"')\\\">"
	        "Edit Source</button></div>\";"
	        "h+=\"</div>\";"
	        "document.getElementById('card').innerHTML=h;}"
	        "function send(a){window.cefQuery({request:'action:'+a,"
	        "onSuccess:()=>{},onFailure:()=>{}});}"
	        "function setActive(el){document.querySelectorAll('.sb-row')"
	        ".forEach(b=>b.classList.remove('on'));el.classList.add('on');}"
	        // Tab switch: hides all .sb-grp, shows the one matching the
	        // tab's data-tab. Re-applies any active filter to the new tab.
	        "function switchTab(t){"
	        "document.querySelectorAll('.tab').forEach(x=>x.classList.remove('on'));"
	        "t.classList.add('on');"
	        "const idx=t.dataset.tab;"
	        "document.querySelectorAll('.sb-grp').forEach(g=>"
	        "g.classList.toggle('on',g.dataset.grp===idx));"
	        "const q=document.getElementById('dexq');"
	        "dexfilter(q?q.value:'');"
	        // Auto-pick the first row of the newly-shown tab so the detail
	        // card / 3D preview update immediately.
	        "const f=document.querySelector('.sb-grp.on .sb-row');"
	        "if(f){clickedKey=f.dataset.key;lastKey=f.dataset.key;"
	        "setActive(f);render(f.dataset.key);send('pick:'+f.dataset.id);}}"
	        // Sticky selection: hover previews until first click; after
	        // that, only another click swaps the stuck preview.
	        "let lastKey='';let clickedKey=null;"
	        "function hover(el){if(clickedKey)return;"
	        "const k=el.dataset.key;if(k===lastKey)return;"
	        "lastKey=k;setActive(el);render(k);send('pick:'+el.dataset.id);}"
	        "function pick(el){const k=el.dataset.key;"
	        "clickedKey=k;lastKey=k;"
	        "setActive(el);render(k);send('pick:'+el.dataset.id);}"
	        // Filter only within the currently-active tab — stops a search
	        // term from forcing the user to scan every group.
	        "function dexfilter(q){q=(q||'').toLowerCase();"
	        "document.querySelectorAll('.sb-grp.on .sb-row').forEach(r=>{"
	        "r.style.display=(!q||r.dataset.q.indexOf(q)>=0)?'':'none';});"
	        "document.querySelectorAll('.sb-grp.on .sb-sub').forEach(h=>{"
	        "let n=h.nextElementSibling,any=false;"
	        "while(n&&n.classList.contains('sb-row')){"
	        "if(n.style.display!=='none')any=true;n=n.nextElementSibling;}"
	        "h.style.display=any?'':'none';});}"
	        "const _f=document.querySelector('.sb-grp.on .sb-row');"
	        "if(_f){_f.classList.add('on');lastKey=_f.dataset.key;"
	        "render(_f.dataset.key);send('pick:'+_f.dataset.id);}"
	        "</script></body></html>";
	return finalizeFileUrl(html);
}

std::string settingsPage(const vk::Game& game) {
	const auto& s = game.settings();
	char buf[64];
	const std::string css =
		"body{justify-content:flex-start;padding:48px 0 60px;"
		"align-items:center;height:auto;min-height:100vh;box-sizing:border-box}"
		"h1{font-size:48px;letter-spacing:6px;margin:0 0 8px}"
		".tag{margin:0 0 24px}"
		".tabs{display:flex;gap:6px;margin-bottom:24px}"
		".tabs button{padding:8px 18px;font-size:13px;letter-spacing:2px;"
		"background:rgba(20,13,8,0.97);color:var(--accent-mid);"
		"border:1px solid var(--accent-mid);font-family:inherit;"
		"cursor:pointer;text-transform:uppercase}"
		".tabs button.on{background:rgba(94,67,30,0.95);"
		"color:var(--accent-hi)}"
		".pane{display:none;width:680px;max-width:90%}"
		".pane.on{display:block}"
		".row{display:flex;align-items:center;justify-content:space-between;"
		"padding:14px 20px;background:rgba(20,13,8,0.97);"
		"border:1px solid rgba(184,136,56,0.4);margin-bottom:8px}"
		".row .lbl{font-size:14px;letter-spacing:2px;color:var(--ink)}"
		".row .ctl{display:flex;align-items:center;gap:12px;"
		"min-width:220px;justify-content:flex-end}"
		".row input[type='range']{width:180px;accent-color:var(--accent-hi)}"
		".row .val{font-family:monospace;color:var(--accent-hi);"
		"font-size:13px;min-width:42px;text-align:right}"
		".tog{position:relative;width:44px;height:22px;"
		"background:rgba(40,30,20,0.9);border:1px solid var(--accent-mid);"
		"border-radius:11px;cursor:pointer;transition:background 0.15s}"
		".tog::after{content:'';position:absolute;left:3px;top:2px;"
		"width:14px;height:14px;border-radius:50%;background:var(--accent-mid);"
		"transition:left 0.15s,background 0.15s}"
		".tog.on{background:rgba(94,67,30,0.95)}"
		".tog.on::after{left:25px;background:var(--accent-hi)}"
		".kvtbl{border-collapse:collapse;margin:0 auto}"
		".kvtbl td{padding:6px 18px;font-size:14px;letter-spacing:1px;"
		"border-bottom:1px solid rgba(184,136,56,0.25)}"
		".kvtbl td:first-child{color:var(--accent-mid);text-align:right;"
		"width:220px}"
		".kvtbl td:last-child{color:var(--ink);font-family:monospace}"
		".back{margin-top:24px;width:160px}"
		".theme-grid{display:grid;grid-template-columns:repeat(auto-fill,"
		"minmax(220px,1fr));gap:14px;width:680px;max-width:90%}"
		".theme-card{background:rgba(20,13,8,0.97);"
		"border:1px solid var(--accent-mid);padding:16px 18px;"
		"cursor:pointer;transition:background 0.15s,transform 0.15s,"
		"border-color 0.15s;font-family:inherit;color:inherit;"
		"text-align:left}"
		".theme-card:hover{background:rgba(94,67,30,0.85);"
		"transform:translateY(-2px);border-color:var(--accent-hi)}"
		".theme-card.on{box-shadow:0 0 0 2px var(--accent-hi) inset}"
		".theme-card .swatches{display:flex;gap:6px;margin-bottom:10px}"
		".theme-card .swatches span{display:block;width:32px;height:24px;"
		"border:1px solid rgba(0,0,0,0.4)}"
		".theme-card h3{margin:0 0 6px;color:var(--accent-hi);"
		"font-size:16px;letter-spacing:2px;font-weight:400}"
		".theme-card p{margin:0;font-size:11px;line-height:1.4;opacity:0.78}";
	std::string html = pageHead(game, css)
	     + "<h1>Settings</h1>"
	       "<div class='tag'>Audio | Network | Controls | Theme</div>"
	       "<div class='tabs'>"
	       "<button class='on' onclick=\"tab(0)\">Audio</button>"
	       "<button onclick=\"tab(1)\">Network</button>"
	       "<button onclick=\"tab(2)\">Controls</button>"
	       "<button onclick=\"tab(3)\">Theme</button>"
	       "</div>"
	       // ── Audio pane ──
	       "<div class='pane on' id='p0'>"
	       "<div class='row'><span class='lbl'>Master Volume</span>"
	       "<span class='ctl'><input type='range' min='0' max='1' step='0.05' "
	       "value='";
	std::snprintf(buf, sizeof(buf), "%.2f", s.master_volume);
	html += buf;
	html += "' oninput=\"slider(this,'master_volume')\">"
	        "<span class='val' id='v_master_volume'>";
	std::snprintf(buf, sizeof(buf), "%.0f%%", s.master_volume * 100);
	html += buf;
	html += "</span></span></div>"
	        "<div class='row'><span class='lbl'>Music Volume</span>"
	        "<span class='ctl'><input type='range' min='0' max='1' step='0.05' "
	        "value='";
	std::snprintf(buf, sizeof(buf), "%.2f", s.music_volume);
	html += buf;
	html += "' oninput=\"slider(this,'music_volume')\">"
	        "<span class='val' id='v_music_volume'>";
	std::snprintf(buf, sizeof(buf), "%.0f%%", s.music_volume * 100);
	html += buf;
	html += "</span></span></div>"
	        "<div class='row'><span class='lbl'>Music Enabled</span>"
	        "<span class='ctl'><div class='tog";
	html += s.music_enabled ? " on" : "";
	html += "' onclick=\"toggle(this,'music_enabled')\"></div></span></div>"
	        "<div class='row'><span class='lbl'>Footsteps</span>"
	        "<span class='ctl'><div class='tog";
	html += s.footsteps_muted ? "" : " on";  // inverted: "on" = audible
	html += "' onclick=\"toggleInv(this,'footsteps_muted')\"></div></span></div>"
	        "<div class='row'><span class='lbl'>Effects (combat / dig / pickup)</span>"
	        "<span class='ctl'><div class='tog";
	html += s.effects_muted ? "" : " on";
	html += "' onclick=\"toggleInv(this,'effects_muted')\"></div></span></div>"
	        "</div>"
	        // ── Network pane ──
	        "<div class='pane' id='p1'>"
	        "<div class='row'><span class='lbl'>Visible to LAN</span>"
	        "<span class='ctl'><div class='tog";
	html += s.lan_visible ? " on" : "";
	html += "' onclick=\"toggle(this,'lan_visible')\"></div></span></div>"
	        "<div class='row' style='opacity:0.6'><span class='lbl'>"
	        "Sim speed cap</span><span class='ctl'><span class='val'>";
	std::snprintf(buf, sizeof(buf), "%.1fx", s.sim_speed_cap);
	html += buf;
	html += "</span></span></div>"
	        "</div>"
	        // ── Controls pane (read-only cheat sheet) ──
	        "<div class='pane' id='p2'>"
	        "<table class='kvtbl'>"
	        "<tr><td>Move</td><td>WASD</td></tr>"
	        "<tr><td>Jump</td><td>Space</td></tr>"
	        "<tr><td>Sprint</td><td>Shift</td></tr>"
	        "<tr><td>Look</td><td>Mouse</td></tr>"
	        "<tr><td>Attack / Place</td><td>LMB / RMB</td></tr>"
	        "<tr><td>Drop</td><td>Q</td></tr>"
	        "<tr><td>Inventory</td><td>Tab</td></tr>"
	        "<tr><td>Handbook</td><td>H</td></tr>"
	        "<tr><td>Camera</td><td>V</td></tr>"
	        "<tr><td>Pause</td><td>Esc</td></tr>"
	        "<tr><td>Debug / Tuning</td><td>F3 / F6</td></tr>"
	        "<tr><td>Screenshot</td><td>F2</td></tr>"
	        "</table></div>"
	        // ── Theme pane ── three preset cards. Hover live-previews via
	        // CSS-var swap on :root; click commits via `set:theme:<id>`.
	        "<div class='pane' id='p3'>"
	        "<div class='theme-grid'>"
	        "<div class='theme-card' data-id='brass' "
	        "data-hi='%23f3c44c' data-mid='%23b88838' data-ink='%23f0e0c0' "
	        "onmouseenter=\"thHover(this)\" onmouseleave=\"thLeave()\" "
	        "onclick=\"thPick(this)\">"
	        "<div class='swatches'>"
	        "<span style='background:%23f3c44c'></span>"
	        "<span style='background:%23b88838'></span>"
	        "<span style='background:%23f0e0c0'></span>"
	        "</div><h3>Brass &amp; Deep Wood</h3>"
	        "<p>The default. Civ6-sepia.</p></div>"
	        "<div class='theme-card' data-id='cobalt' "
	        "data-hi='%237eb8e8' data-mid='%233a6890' data-ink='%23dde6ed' "
	        "onmouseenter=\"thHover(this)\" onmouseleave=\"thLeave()\" "
	        "onclick=\"thPick(this)\">"
	        "<div class='swatches'>"
	        "<span style='background:%237eb8e8'></span>"
	        "<span style='background:%233a6890'></span>"
	        "<span style='background:%23dde6ed'></span>"
	        "</div><h3>Cobalt &amp; Steel</h3>"
	        "<p>Cool blues. Reads like a UI pattern guide.</p></div>"
	        "<div class='theme-card' data-id='lichen' "
	        "data-hi='%23d4cf9f' data-mid='%238a9970' data-ink='%23ede5d0' "
	        "onmouseenter=\"thHover(this)\" onmouseleave=\"thLeave()\" "
	        "onclick=\"thPick(this)\">"
	        "<div class='swatches'>"
	        "<span style='background:%23d4cf9f'></span>"
	        "<span style='background:%238a9970'></span>"
	        "<span style='background:%23ede5d0'></span>"
	        "</div><h3>Bone &amp; Lichen</h3>"
	        "<p>Dusty ranger. Forest scout vibes.</p></div>"
	        "</div></div>"
	        "<button class='btn back' onclick=\"send('back')\">Back</button>";
	html += "<script>"
	        "function tab(i){"
	        "document.querySelectorAll('.tabs button').forEach((b,j)=>"
	        "b.classList.toggle('on',j==i));"
	        "document.querySelectorAll('.pane').forEach((p,j)=>"
	        "p.classList.toggle('on',j==i));}"
	        "function slider(el,key){"
	        "const v=parseFloat(el.value);"
	        "document.getElementById('v_'+key).textContent="
	        "Math.round(v*100)+'%25';"
	        "send('set:'+key+':'+v);}"
	        "function toggle(el,key){"
	        "const on=!el.classList.contains('on');"
	        "el.classList.toggle('on',on);"
	        "send('set:'+key+':'+(on?'true':'false'));}"
	        "function toggleInv(el,key){"  // UI-on means feature ENABLED → key (muted) is false
	        "const on=!el.classList.contains('on');"
	        "el.classList.toggle('on',on);"
	        "send('set:'+key+':'+(on?'false':'true'));}"
	        // Theme: hover live-previews via CSS vars on :root; click
	        // commits + persists via the `set:theme:<id>` action handler.
	        "function applyTheme(hi,mid,ink){"
	        "const r=document.documentElement.style;"
	        "r.setProperty('--accent-hi',hi);"
	        "r.setProperty('--accent-mid',mid);"
	        "r.setProperty('--ink',ink);}"
	        "function thHover(c){applyTheme(c.dataset.hi,c.dataset.mid,c.dataset.ink);}"
	        "function thLeave(){const c=document.querySelector('.theme-card.on');"
	        "if(c)applyTheme(c.dataset.hi,c.dataset.mid,c.dataset.ink);}"
	        "function thPick(c){"
	        "document.querySelectorAll('.theme-card').forEach(x=>x.classList.remove('on'));"
	        "c.classList.add('on');"
	        "applyTheme(c.dataset.hi,c.dataset.mid,c.dataset.ink);"
	        "send('set:theme:'+c.dataset.id);}"
	        "(()=>{const id='" + s.theme_id + "';"
	        "const c=document.querySelector('.theme-card[data-id=\"'+id+'\"]')||"
	        "document.querySelector('.theme-card[data-id=\"brass\"]');"
	        "if(c)c.classList.add('on');})();"
	        "</script>";
	return finalizeFileUrl(html + pageTail());
}

std::string charSelectPage(const vk::Game& game) {
	// Materialise the playable subset of "living" with the stats the
	// detail card needs. Sorting by name keeps the sidebar deterministic
	// across boots.
	struct PlayableItem {
		std::string id, name, desc;
		int str=0, sta=0, agi=0, intl=0;     // 0–5 stat bars
		float walk=0, run=0;                  // m/s
		std::string features;                 // comma-joined tag list
	};
	auto readF = [](const solarium::ArtifactEntry& e, const char* k) {
		auto it = e.fields.find(k);
		return it == e.fields.end() ? 0.0f : (float)std::atof(it->second.c_str());
	};
	auto readI = [](const solarium::ArtifactEntry& e, const char* k) {
		auto it = e.fields.find(k);
		return it == e.fields.end() ? 0 : std::atoi(it->second.c_str());
	};
	std::vector<PlayableItem> playables;
	for (auto* e : game.artifactRegistry().byCategory("living")) {
		if (!e->flag("playable")) continue;
		PlayableItem p;
		p.id   = e->id;
		p.name = e->name.empty() ? e->id : e->name;
		p.desc = e->text("description");
		p.str  = readI(*e, "stats_strength");
		p.sta  = readI(*e, "stats_stamina");
		p.agi  = readI(*e, "stats_agility");
		p.intl = readI(*e, "stats_intelligence");
		p.walk = readF(*e, "walk_speed");
		p.run  = readF(*e, "run_speed");
		p.features = e->text("features");
		playables.push_back(std::move(p));
	}
	std::sort(playables.begin(), playables.end(),
		[](const PlayableItem& a, const PlayableItem& b){ return a.name < b.name; });

	// Per-page CSS — stat bars, speed line, feature chips.
	const std::string css =
		".stats{display:grid;grid-template-columns:auto 1fr auto;"
		"column-gap:14px;row-gap:6px;margin-top:18px;max-width:520px;"
		"font-size:12px;align-items:center}"
		".stats .lbl{color:var(--accent-mid);text-transform:uppercase;"
		"letter-spacing:2px;font-family:monospace}"
		".stats .bar{height:10px;background:rgba(40,30,20,0.85);"
		"border:1px solid rgba(184,136,56,0.5);position:relative;"
		"box-sizing:border-box}"
		".stats .bar .fill{position:absolute;top:0;left:0;bottom:0;"
		"background:linear-gradient(90deg,var(--accent-mid),var(--accent-hi))}"
		".stats .num{color:var(--accent-hi);font-family:monospace;"
		"font-size:13px;min-width:24px;text-align:right}"
		".speed{display:flex;gap:24px;margin-top:14px;font-size:12px;"
		"color:var(--accent-mid);font-family:monospace;letter-spacing:1px;"
		"text-transform:uppercase}"
		".speed b{color:var(--accent-hi);font-weight:400;font-size:14px;"
		"margin-left:6px}"
		".features{margin-top:14px;display:flex;flex-wrap:wrap;gap:6px}"
		".features span{padding:4px 10px;font-size:11px;"
		"background:rgba(94,67,30,0.88);border:1px solid var(--accent-mid);"
		"color:var(--accent-hi);letter-spacing:1px}";
	std::string html = pageHeadDex(game, css)
	     + "<aside class='sidebar'>"
	       "<div class='sb-head'><h1>Choose Character</h1>"
	       "<div class='sub'>" + std::to_string(playables.size()) +
	       " playable" + (playables.size() == 1 ? "" : "s") + "</div></div>"
	       "<div class='sb-list'>";
	for (size_t i = 0; i < playables.size(); ++i) {
		const auto& p = playables[i];
		html += "<button class='sb-row" + std::string(i == 0 ? " on" : "") +
		        "' data-id='" + p.id + "' "
		        "onclick=\"pick('" + p.id + "',this)\" "
		        "onmouseenter=\"hover('" + p.id + "',this)\">" +
		        p.name + "<span class='id'>" + p.id + "</span></button>";
	}
	html += "</div>"
	        "<div class='sb-foot'>"
	        "<button class='primary' onclick=\"send('play')\">Begin Game</button>"
	        "<button onclick=\"send('back')\">Back</button>"
	        "</div></aside>"
	        "<main class='detail'>"
	        "<div class='detail-card' id='card'></div></main>"
	     + versionTag();
	// Inline JS — ENTRIES table + render() + sticky-selection.
	html += "<script>const ENTRIES={";
	for (size_t i = 0; i < playables.size(); ++i) {
		const auto& p = playables[i];
		if (i) html += ",";
		char buf[64];
		std::snprintf(buf, sizeof(buf), "%.1f", p.walk);
		std::string walkS = buf;
		std::snprintf(buf, sizeof(buf), "%.1f", p.run);
		std::string runS  = buf;
		html += "'" + p.id + "':{name:'" + enc(compact(p.name, 80)) +
		        "',desc:'" + enc(compact(p.desc, 400)) +
		        "',str:" + std::to_string(p.str) +
		        ",sta:" + std::to_string(p.sta) +
		        ",agi:" + std::to_string(p.agi) +
		        ",intl:" + std::to_string(p.intl) +
		        ",walk:'" + walkS + "',run:'" + runS +
		        "',feat:'" + enc(compact(p.features, 200)) + "'}";
	}
	html += "};"
	        "function bar(label,n){"
	        // 5 is the per-stat ceiling shown in artifacts; clamp anything
	        // silly to 100% so the bar never overflows visually.
	        "const pct=Math.min(100,Math.max(0,n*20));"
	        "return \"<span class='lbl'>\"+label+\"</span>\"+"
	        "\"<span class='bar'><span class='fill' style='width:\"+pct+\"%25'></span></span>\"+"
	        "\"<span class='num'>\"+n+\"</span>\";}"
	        "function render(id){"
	        "const e=ENTRIES[id];if(!e)return;"
	        "let h=\"<span class='badge'>Living</span>\"+"
	        "\"<span class='badge'>\"+id+\"</span>\"+"
	        "\"<h2>\"+e.name+\"</h2>\";"
	        "if(e.desc)h+=\"<div class='desc'>\"+e.desc+\"</div>\";"
	        "if(e.str||e.sta||e.agi||e.intl){"
	        "h+=\"<div class='stats'>\"+bar('STR',e.str)+bar('STA',e.sta)+"
	        "bar('AGI',e.agi)+bar('INT',e.intl)+\"</div>\";}"
	        "if(e.walk||e.run){"
	        "h+=\"<div class='speed'>walk<b>\"+e.walk+\"</b>m/s\";"
	        "if(e.run!=='0.0')h+=\"  run<b>\"+e.run+\"</b>m/s\";"
	        "h+=\"</div>\";}"
	        "if(e.feat){const tags=e.feat.split(',').map(s=>s.trim()).filter(Boolean);"
	        "if(tags.length){h+=\"<div class='features'>\"+"
	        "tags.map(t=>'<span>'+t+'</span>').join('')+\"</div>\";}}"
	        "document.getElementById('card').innerHTML=h;}"
	        "function send(a){window.cefQuery({request:'action:'+a,"
	        "onSuccess:()=>{},onFailure:()=>{}});}"
	        "function setActive(el){document.querySelectorAll('.sb-row')"
	        ".forEach(b=>b.classList.remove('on'));el.classList.add('on');}"
	        // Sticky-selection: hover previews while clickedId is null;
	        // once the user clicks, that row is stuck — further hovers are
	        // no-ops, only another click swaps the preview.
	        "let lastHover='';let clickedId=null;"
	        "function hover(id,el){if(clickedId)return;"
	        "if(id===lastHover)return;lastHover=id;"
	        "setActive(el);render(id);send('pick:'+id);}"
	        "function pick(id,el){clickedId=id;lastHover=id;"
	        "setActive(el);render(id);send('pick:'+id);}"
	        "render('" + (playables.empty() ? "" : playables[0].id) + "');"
	        "</script></body></html>";
	return finalizeFileUrl(html);
}

std::string editorUrlFor(const vk::Game& game,
                         const std::string& cat,
                         const std::string& id) {
	const solarium::ArtifactEntry* e = game.artifactRegistry().findById(id);
	if (!e || e->category != cat) {
		std::fprintf(stderr, "[edit] unknown %s:%s\n",
		             cat.c_str(), id.c_str());
		return "";
	}
	std::ifstream src(e->filePath);
	std::stringstream sb; sb << src.rdbuf();
	std::string code = sb.str();
	// Escape the source for a JS template literal (`...`).
	auto jsEscape = [](const std::string& s) {
		std::string o; o.reserve(s.size() + 16);
		for (char c : s) {
			if (c == '\\' || c == '`') { o += '\\'; o += c; }
			else if (c == '$')         { o += "\\$"; }
			else                        o += c;
		}
		return o;
	};
	// Resolve absolute path to Monaco's vs/ — game cwd is build/, the
	// submodule lives at repo-root/third_party/...
	std::filesystem::path mvs = std::filesystem::absolute(
		std::filesystem::path(game.execDir()) /
		".." / "third_party" / "monaco-editor" / "out" /
		"monaco-editor" / "min" / "vs");
	std::string vsPath = mvs.string();
	std::ostringstream html;
	// Editor page is loaded via file://, NOT data:, so it can have raw '#'
	// hex colours in CSS. Hex codes here are passed through unescaped.
	html << "<!doctype html><html><head><meta charset='utf-8'>"
	     << "<title>Edit " << id << "</title>"
	     << "<style>"
	     << "html,body{margin:0;height:100vh;background:#0a0703;"
	     << "color:#f0e0c0;font-family:Georgia,serif;display:flex;"
	     << "flex-direction:column}"
	     << ".bar{display:flex;align-items:center;gap:14px;"
	     << "padding:10px 18px;background:rgba(20,13,8,0.97);"
	     << "border-bottom:1px solid #b88838}"
	     << ".bar h2{margin:0;font-size:18px;letter-spacing:3px;"
	     << "color:#f3c44c;font-weight:400}"
	     << ".bar .id{font-size:11px;color:#b88838;"
	     << "font-family:monospace;letter-spacing:1px;opacity:0.7}"
	     << ".bar .grow{flex:1}"
	     << ".bar button{padding:8px 18px;font-size:13px;"
	     << "letter-spacing:2px;background:rgba(94,67,30,0.85);"
	     << "color:#f3c44c;border:1px solid #b88838;cursor:pointer;"
	     << "font-family:inherit;text-transform:uppercase}"
	     << ".bar button:hover{background:rgba(94,67,30,1)}"
	     << "#editor{flex:1}"
	     << "</style></head><body>"
	     << "<div class='bar'>"
	     << "<h2>" << (e->name.empty() ? id : e->name) << "</h2>"
	     << "<span class='id'>" << cat << "/" << id << "</span>"
	     << "<span class='grow'></span>"
	     << "<button onclick=\"saveAndBack()\">Save</button>"
	     << "<button onclick=\"send('back')\">Cancel</button>"
	     << "</div>"
	     << "<div id='editor'></div>"
	     << "<script src='file://" << vsPath << "/loader.js'></script>"
	     << "<script>"
	     << "function send(a){window.cefQuery({request:'action:'+a,"
	     << "onSuccess:()=>{},onFailure:()=>{}});}"
	     << "let ed;"
	     << "require.config({paths:{vs:'file://" << vsPath << "'}});"
	     << "require(['vs/editor/editor.main'],function(){"
	     << "ed=monaco.editor.create(document.getElementById('editor'),{"
	     << "value:`" << jsEscape(code) << "`,"
	     << "language:'python',theme:'vs-dark',automaticLayout:true,"
	     << "minimap:{enabled:true},fontSize:14});});"
	     << "function saveAndBack(){"
	     << "if(!ed)return;"
	     << "const t=ed.getValue();"
	     << "send('save_artifact:" << cat << ":" << id
	     << ":'+btoa(unescape(encodeURIComponent(t))));}"
	     << "</script></body></html>";
	std::string path = "/tmp/solarium_editor.html";
	std::ofstream f(path);
	f << html.str();
	f.close();
	return std::string("file://") + path;
}

std::string modManagerPage(const vk::Game& game) {
	auto namespaces = game.artifactRegistry().discoverNamespaces("artifacts");
	std::sort(namespaces.begin(), namespaces.end());
	// Per-namespace count, derived from each entry's filePath
	// (artifacts/<cat>/<ns>/<file>.py).
	std::unordered_map<std::string, int> nsCount;
	for (const auto& e : game.artifactRegistry().entries()) {
		auto p = std::filesystem::path(e.filePath);
		if (p.has_parent_path()) {
			nsCount[p.parent_path().filename().string()]++;
		}
	}
	// Currently-disabled namespaces, parsed from comma-joined settings.
	std::set<std::string> disabled;
	{
		const std::string& s = game.settings().disabled_mods;
		size_t pos = 0;
		while (pos < s.size()) {
			size_t q = s.find(',', pos);
			std::string tok = s.substr(pos,
				q == std::string::npos ? std::string::npos : q - pos);
			while (!tok.empty() && tok.front() == ' ') tok.erase(tok.begin());
			while (!tok.empty() && tok.back() == ' ')  tok.pop_back();
			if (!tok.empty()) disabled.insert(tok);
			if (q == std::string::npos) break;
			pos = q + 1;
		}
	}
	const std::string css =
		"body{justify-content:flex-start;padding:60px 60px 60px;"
		"height:auto;min-height:100vh;box-sizing:border-box;align-items:center}"
		"h1{font-size:48px;letter-spacing:6px;margin:0 0 8px}"
		".tag{margin:0 0 24px}"
		".list{display:flex;flex-direction:column;gap:8px;width:680px}"
		".row{display:flex;align-items:center;justify-content:space-between;"
		"padding:14px 22px;background:rgba(20,13,8,0.97);"
		"border:1px solid var(--accent-mid)}"
		".row.off{opacity:0.55}"
		".row .nm{font-size:18px;color:var(--accent-hi);letter-spacing:2px;"
		"font-family:monospace}"
		".row .ct{font-size:11px;color:var(--accent-mid);letter-spacing:1px;"
		"font-family:monospace;margin-left:12px}"
		// Toggle pill — knob slides on .on.
		".tog{position:relative;width:44px;height:22px;"
		"background:rgba(40,30,20,0.9);border:1px solid var(--accent-mid);"
		"border-radius:11px;cursor:pointer;transition:background 0.15s}"
		".tog::after{content:'';position:absolute;left:3px;top:2px;"
		"width:14px;height:14px;border-radius:50%;background:var(--accent-mid);"
		"transition:left 0.15s,background 0.15s}"
		".tog.on{background:rgba(94,67,30,0.95)}"
		".tog.on::after{left:25px;background:var(--accent-hi)}"
		".back{margin-top:24px;width:160px}";
	std::string html = pageHead(game, css)
	     + "<h1>Mod Manager</h1>"
	       "<div class='tag'>Enable / disable artifact namespaces</div>"
	       "<div class='note'>Changes apply on next launch</div>"
	       "<div class='list'>";
	if (namespaces.empty()) {
		html += "<div class='row'><span class='nm'>(none found)</span>"
		        "<span class='ct'>0 entries</span></div>";
	}
	for (const auto& ns : namespaces) {
		bool isOff = disabled.count(ns) > 0;
		int  ct    = nsCount.count(ns) ? nsCount[ns] : 0;
		html += "<div class='row" + std::string(isOff ? " off" : "") +
		        "'><span><span class='nm'>" + enc(ns) +
		        "</span><span class='ct'>" + std::to_string(ct) +
		        " entries</span></span>"
		        "<div class='tog" + std::string(isOff ? "" : " on") +
		        "' onclick=\"tog(this,'" + enc(ns) + "')\"></div>"
		        "</div>";
	}
	html += "</div>"
	        "<button class='btn back' onclick=\"send('back')\">Back</button>"
	        "<script>"
	        "function tog(el,ns){"
	        "const on=!el.classList.contains('on');"
	        "el.classList.toggle('on',on);"
	        "el.parentElement.classList.toggle('off',!on);"
	        "send('mod:'+ns+':'+(on?'on':'off'));}"
	        "</script>";
	return finalizeFileUrl(html + pageTail());
}

std::string lobbyPage(const vk::Game& game) {
	const std::string css =
		"body{justify-content:center;align-items:center}"
		"h1{font-size:56px;letter-spacing:8px;margin:0 0 12px}"
		".tag{margin:0 0 32px;font-size:14px;letter-spacing:2px;"
		"color:var(--accent-mid)}"
		".players{display:flex;flex-direction:column;gap:6px;"
		"width:380px;margin-bottom:24px}"
		".player{padding:12px 18px;background:rgba(20,13,8,0.97);"
		"border:1px solid var(--accent-mid);color:var(--accent-hi);"
		"font-family:monospace;font-size:14px;letter-spacing:1px}"
		".btn{width:280px}";
	return finalizeFileUrl(pageHead(game, css)
	     + "<h1>Lobby</h1>"
	       "<div class='tag'>Your LAN game is live - joiners can find you on UDP 7778</div>"
	       "<div class='players'>"
	       "<div class='player'>You (host)</div>"
	       "</div>"
	       "<button class='btn' onclick=\"send('lobby_start')\">Start Game</button>"
	       "<button class='btn back' onclick=\"send('main_menu')\">Cancel</button>"
	     + pageTail());
}

// ─────────────────────────────────────────────────────────────────────────
// PageCache — pre-bake the static page URLs at boot so we don't allocate
// 50 KB of HTML per navigation.
// ─────────────────────────────────────────────────────────────────────────
PageCache::PageCache(const vk::Game& g)
    : main(mainPage(g))
    , chars(charSelectPage(g))
    , handbook(handbookPage(g))
    , worldPicker(worldPickerPage(g))
    , pause(pausePage(g))
    , death(deathPage(g))
    , mpHub(multiplayerHubPage(g))
    , lobby(lobbyPage(g)) {}

// ─────────────────────────────────────────────────────────────────────────
// Per-section register helpers. Each captures only the deps it needs
// (router + game/cef/win/pages/firstPlayableId, as relevant). Co-locates
// the handlers with the pages they drive. registerActions() is now a
// thin dispatcher that calls each in order.

namespace {

void registerSystemActions(ActionRouter& router,
                           vk::Game& game,
                           vk::CefHost* cef,
                           ::GLFWwindow* win,
                           const PageCache& pages) {
	using MS = solarium::vk::MenuScreen;
	router.on("quit", [win](const std::string&) {
		glfwSetWindowShouldClose(win, GLFW_TRUE);
		return true;
	});
	router.on("resume", [&game](const std::string&) {
		// Pause-menu Resume — drop the overlay and hand control back.
		game.setCefMenuActive(false);
		return true;
	});
	router.on("main_menu", [&game](const std::string&) {
		// Pause-menu / Death "Main Menu" — disconnect, kill any
		// hosted subprocess, rewind to the title.
		game.returnToMainMenu();
		return true;
	});
	router.on("respawn", [&game](const std::string&) {
		// Death overlay Respawn button.
		game.setCefMenuActive(false);
		game.respawn();
		return true;
	});
	router.on("back", [&game, cef, &pages](const std::string&) {
		// Back from a CEF sub-screen has two meanings:
		//   * Boot flow (Menu state) — return to the main title.
		//   * In-game (Playing state, H opened the handbook over
		//     the world) — just dismiss CEF; don't drop the player
		//     back to the title.
		if (game.state() == solarium::vk::GameState::Playing) {
			game.setCefMenuActive(false);
			game.setPreviewId("");
			game.setPreviewClip("");
		} else {
			game.setMenuScreen(MS::Main);
			game.setPreviewId("");
			game.setPreviewClip("");
			cef->loadUrl(pages.main);
		}
		return true;
	});
}

void registerEditorActions(ActionRouter& router,
                           vk::Game& game,
                           vk::CefHost* cef,
                           const PageCache& pages) {
	using MS = solarium::vk::MenuScreen;
	router.onPrefix("edit", [&game, cef](const std::string& body) {
		// "edit:<cat>:<id>" — Monaco-backed editor. Has to load via
		// file:// (the AMD loader fetches sibling files).
		auto p = EditPayload::parse(body);
		if (!p) return false;
		std::string url = editorUrlFor(game, p->cat, p->id);
		if (!url.empty()) cef->loadUrl(url);
		return true;
	});
	router.onPrefix("save_artifact", [&game, cef, &pages](const std::string& body) {
		// "save_artifact:<cat>:<id>:<base64-source>"
		auto p = SaveArtifactPayload::parse(body);
		if (!p) return false;
		std::string src = base64Decode(p->base64Source);
		// Save as a fork in the user's persistent registry overlay
		// (~/.solarium/forks/<dirName>/<id>.py). See
		// ArtifactRegistry::loadForks. TODO(cloud): mirror to a per-
		// account cloud store so forks travel between devices.
		// `cat` is singular ("item"); folders are plural ("items").
		const std::string& cat = p->cat;
		std::string dirName =
		    (cat == "item")     ? "items" :
		    (cat == "block")    ? "blocks" :
		    (cat == "behavior") ? "behaviors" :
		    (cat == "effect")   ? "effects" :
		    (cat == "resource") ? "resources" :
		    (cat == "world")    ? "worlds" :
		    (cat == "annotation")? "annotations" :
		    (cat == "structure")? "structures" :
		    (cat == "model")    ? "models" : cat;
		std::string forksRoot = solarium::ArtifactRegistry::defaultForksRoot();
		if (forksRoot.empty()) {
			std::fprintf(stderr, "[edit] HOME unset; cannot save fork\n");
			return false;
		}
		std::string outDir = forksRoot + "/" + dirName;
		std::error_code ec;
		std::filesystem::create_directories(outDir, ec);
		std::string outPath = outDir + "/" + p->id + ".py";
		std::ofstream of(outPath);
		of << src; of.close();
		std::printf("[edit] fork %s (%zu bytes)\n",
		            outPath.c_str(), src.size());
		// Hot-reload — base scan + fork overlay.
		auto& reg = const_cast<solarium::ArtifactRegistry&>(
		    game.artifactRegistry());
		reg.loadAll("artifacts");
		reg.loadForks(forksRoot);
		// Return to handbook (rebuild fresh so changes show up).
		game.setMenuScreen(MS::Handbook);
		cef->loadUrl(pages.handbook);
		return true;
	});
}

// Save slots → world picker → char-select → connect. server-spawn
// fires inside the `load:<path>` or `world:<id>` handlers below.
void registerSaveActions(ActionRouter& router,
                         vk::Game& game,
                         vk::CefHost* cef,
                         const PageCache& pages,
                         const std::string& firstPlayableId) {
	using MS = solarium::vk::MenuScreen;
	router.on("singleplayer", [&game, cef](const std::string&) {
		game.setMenuScreen(MS::Main);
		game.setPreviewId("");
		game.setPreviewClip("");
		game.setNextHostLanVisible(false);
		cef->loadUrl(saveSlotsPage(game));
		return true;
	});
	router.on("new_world", [cef, &pages](const std::string&) {
		cef->loadUrl(pages.worldPicker);
		return true;
	});
	router.onPrefix("load", [&game, cef, &pages, &firstPlayableId](const std::string& path) {
		// "load:saves/foo" — host with cfg.worldPath set; server's
		// `--world PATH` codepath restores instead of generating fresh.
		solarium::AgentManager::Config cfg;
		cfg.execDir   = game.execDir();
		cfg.worldPath = path;
		// Per-save mod list — pull disabledMods from the save's
		// world.json so this save plays with the namespace set it was
		// last saved with, ignoring the global Mod Manager toggle.
		auto saves = solarium::vk::scanSaves("saves");
		for (const auto& s : saves) {
			if (s.path == path) { cfg.disabledMods = s.disabledMods; break; }
		}
		if (!game.hostLocalServer(cfg)) {
			std::fprintf(stderr,
			             "[cef] load:%s — hostLocalServer failed\n", path.c_str());
			return false;
		}
		game.setMenuScreen(MS::CharacterSelect);
		game.setPreviewClip("wave");
		if (!firstPlayableId.empty())
			game.setPreviewId(firstPlayableId);
		cef->loadUrl(pages.chars);
		return true;
	});
	router.onPrefix("world", [&game, cef, &pages, &firstPlayableId](const std::string& body) {
		// "world:<id>[:<seed>:<villagers>[:<urlencoded-name>]]" — the
		// picker hijacks tile clicks to append the option strip's
		// values. Plain form kept for SOLARIUM_BOOT_PAGE=worlds.
		auto parsed = WorldPayload::parse(body);
		if (!parsed) return false;
		int idx = solarium::worldTemplateIndexOf(parsed->id);
		if (idx < 0) {
			std::fprintf(stderr,
			             "[cef] world:%s — unknown template\n", parsed->id.c_str());
			return false;
		}
		solarium::AgentManager::Config cfg;
		cfg.templateIndex = idx;
		cfg.execDir       = game.execDir();
		cfg.seed          = parsed->seed;
		cfg.debugMobCount = parsed->villagers;

		// URL-decode name and create a save dir BEFORE spawning the
		// server. The save dir's path becomes --world PATH; without
		// it a later "Continue" would have nothing to load.
		std::string name = urlDecode(parsed->name);
		std::string templateName = solarium::worldTemplateIdAt(idx);
		// Snapshot the user's current mod toggles into the save's
		// world.json so this save plays consistently next time.
		cfg.worldPath = solarium::vk::createSave(
		    "saves", name, cfg.seed, idx, templateName,
		    game.settings().disabled_mods);
		cfg.disabledMods = game.settings().disabled_mods;
		if (cfg.worldPath.empty()) {
			std::fprintf(stderr, "[cef] world:%s — createSave failed\n", parsed->id.c_str());
			return false;
		}
		std::printf("[cef] new save at %s (\"%s\")\n",
		            cfg.worldPath.c_str(), name.c_str());
		if (!game.hostLocalServer(cfg)) {
			std::fprintf(stderr,
			             "[cef] world:%s — hostLocalServer failed\n", parsed->id.c_str());
			return false;
		}
		game.setMenuScreen(MS::CharacterSelect);
		game.setPreviewClip("wave");
		if (!firstPlayableId.empty())
			game.setPreviewId(firstPlayableId);
		cef->loadUrl(pages.chars);
		return true;
	});
	router.onPrefix("delete_save", [&game, cef](const std::string& path) {
		// "delete_save:saves/foo" — rm -rf the dir, then refresh.
		std::uintmax_t n = solarium::vk::deleteSave(path);
		std::printf("[cef] deleted save %s (%ju entries)\n",
		            path.c_str(), (uintmax_t)n);
		cef->loadUrl(saveSlotsPage(game));
		return true;
	});
}

void registerMultiplayerActions(ActionRouter& router,
                                vk::Game& game,
                                vk::CefHost* cef,
                                const PageCache& pages,
                                const std::string& firstPlayableId) {
	using MS = solarium::vk::MenuScreen;
	router.on("multiplayer", [&game, cef, &pages](const std::string&) {
		// Multiplayer hub: Host vs Join split.
		game.setMenuScreen(MS::Main);
		game.setPreviewId("");
		game.setPreviewClip("");
		game.setNextHostLanVisible(false);
		cef->loadUrl(pages.mpHub);
		return true;
	});
	router.on("mp_host", [&game, cef, &pages](const std::string&) {
		game.setNextHostLanVisible(true);
		cef->loadUrl(pages.worldPicker);
		return true;
	});
	router.on("mp_join", [&game, cef](const std::string&) {
		// Rebuild fresh each click — picks up newly-discovered LAN servers.
		game.setNextHostLanVisible(false);
		cef->loadUrl(multiplayerPage(game));
		return true;
	});
	router.onPrefix("join", [&game, cef, &pages, &firstPlayableId](const std::string& rest) {
		// "join:192.168.1.5:7777"
		auto p = JoinPayload::parse(rest);
		if (!p) return false;
		game.joinRemoteServer(p->host, p->port);
		game.setMenuScreen(MS::CharacterSelect);
		game.setPreviewClip("wave");
		if (!firstPlayableId.empty())
			game.setPreviewId(firstPlayableId);
		cef->loadUrl(pages.chars);
		return true;
	});
}

void registerSubscreenActions(ActionRouter& router,
                              vk::Game& game,
                              vk::CefHost* cef,
                              const PageCache& pages) {
	using MS = solarium::vk::MenuScreen;
	router.on("handbook", [&game, cef, &pages](const std::string&) {
		game.setMenuScreen(MS::Handbook);
		game.setPreviewClip("");
		cef->loadUrl(pages.handbook);
		return true;
	});
	router.on("settings", [&game, cef](const std::string&) {
		cef->loadUrl(settingsPage(game));
		return true;
	});
	router.on("mods", [&game, cef](const std::string&) {
		cef->loadUrl(modManagerPage(game));
		return true;
	});

	// ── Toggles, picker hover, settings sliders ─────────────────────────
	router.onPrefix("mod", [&game](const std::string& body) {
		// "mod:<ns>:on" / "mod:<ns>:off" — toggle a namespace in
		// Settings.disabled_mods (comma-joined). Changes take effect
		// on next launch (registry loads once at boot).
		auto p = ModPayload::parse(body);
		if (!p) return false;
		auto& s = game.settings();
		std::vector<std::string> toks;
		size_t pos = 0;
		while (pos < s.disabled_mods.size()) {
			size_t q = s.disabled_mods.find(',', pos);
			std::string tok = s.disabled_mods.substr(
			    pos, q == std::string::npos ? std::string::npos : q - pos);
			while (!tok.empty() && tok.front() == ' ') tok.erase(tok.begin());
			while (!tok.empty() && tok.back() == ' ')  tok.pop_back();
			if (!tok.empty()) toks.push_back(tok);
			if (q == std::string::npos) break;
			pos = q + 1;
		}
		auto it = std::find(toks.begin(), toks.end(), p->ns);
		if (p->wantOn) { if (it != toks.end()) toks.erase(it); }
		else           { if (it == toks.end()) toks.push_back(p->ns); }
		std::string joined;
		for (size_t i = 0; i < toks.size(); ++i) {
			if (i) joined += ",";
			joined += toks[i];
		}
		s.disabled_mods = joined;
		s.save();
		std::printf("[mods] %s %s (disabled now: '%s')\n",
		            p->wantOn ? "enabled" : "disabled", p->ns.c_str(), joined.c_str());
		return true;
	});
	router.onPrefix("set", [&game](const std::string& body) {
		// "set:master_volume:0.5" / "set:footsteps_muted:true" —
		// mutate Settings, persist, live-apply to AudioManager.
		auto p = SetPayload::parse(body);
		if (!p) return false;
		auto& s = game.settings();
		const std::string& key = p->key;
		const std::string& val = p->value;
		const bool  b = p->valueBool();
		const float f = p->valueFloat();
		if      (key == "master_volume")   { s.master_volume   = f; game.audio().setMasterVolume(f); }
		else if (key == "music_volume")    { s.music_volume    = f; game.audio().setMusicVolume(f); }
		else if (key == "music_enabled")   { s.music_enabled   = b; if (b) game.audio().startMusic(); else game.audio().stopMusic(); }
		else if (key == "footsteps_muted") { s.footsteps_muted = b; game.audio().setFootstepsMuted(b); }
		else if (key == "effects_muted")   { s.effects_muted   = b; game.audio().setEffectsMuted(b); }
		else if (key == "lan_visible")     { s.lan_visible     = b; }
		else if (key == "theme")           { s.theme_id        = val; }
		else {
			std::fprintf(stderr, "[set] unknown key: %s\n", key.c_str());
			return false;
		}
		s.save();
		return true;
	});
	router.onPrefix("pick", [&game](const std::string& id) {
		// Preview-only: swap the plaza-injected model. Camera pin in
		// game_vk.cpp keeps framing it. Used by both handbook (hover/
		// click) and char-select (avatar tile click).
		game.setPreviewId(id);
		return true;
	});
}

void registerCharSelectActions(ActionRouter& router,
                               vk::Game& game,
                               vk::CefHost* cef,
                               const PageCache& pages) {
	using MS = solarium::vk::MenuScreen;
	router.on("play", [&game, cef, &pages](const std::string&) {
		// LAN hosts route through the lobby first; SP + joiners go
		// straight to Connecting.
		const std::string id = game.previewId();
		if (id.empty()) return false;
		if (game.isLanHost()) {
			cef->loadUrl(pages.lobby);
		} else if (game.beginConnectAs(id)) {
			game.setMenuScreen(MS::Connecting);
			game.setCefMenuActive(false);
		}
		return true;
	});
	router.on("lobby_start", [&game](const std::string&) {
		const std::string id = game.previewId();
		if (id.empty() || !game.beginConnectAs(id)) return false;
		game.setMenuScreen(MS::Connecting);
		game.setCefMenuActive(false);
		return true;
	});
}

} // namespace

// registerActions — wire all page actions onto the router. Called once
// at boot. Thin dispatcher: each per-section helper above owns the
// handlers for its page family.
void registerActions(ActionRouter& router,
                     vk::Game& game,
                     vk::CefHost* cef,
                     ::GLFWwindow* win,
                     const PageCache& pages,
                     const std::string& firstPlayableId) {
	registerSystemActions     (router, game, cef, win, pages);
	registerEditorActions     (router, game, cef, pages);
	registerSaveActions       (router, game, cef, pages, firstPlayableId);
	registerMultiplayerActions(router, game, cef, pages, firstPlayableId);
	registerSubscreenActions  (router, game, cef, pages);
	registerCharSelectActions (router, game, cef, pages);
}

} // namespace solarium::ui
