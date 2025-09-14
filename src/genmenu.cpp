// SPDX-License-Identifier: GPL-2.0-only
/*
 * Emulator's GUI
 *
 * Copyright (C) 2024 Paul Cercueil <paul@crapouillou.net>
 */

extern "C" {
#include <math.h>
}

#include <kos/fs.h>

#include <tsu/genmenu.h>
#include <tsu/font.h>

#include <tsu/drawables/label.h>
#include <tsu/anims/logxymover.h>
#include <tsu/anims/expxymover.h>
#include <tsu/anims/alphafader.h>
#include <tsu/triggers/death.h>

#include <functional>
#include <vector>

#include "background.h"
#include "emu.h"
#include "genmenu.h"
#include "bloom-config.h"

#define MENU_OFF_X 200
#define MENU_OFF_Y 200

#define MENU_ENTRY_SIZE 32
#define ENTRY_SIZE 24

#define TOP_PATH "/"

static std::shared_ptr<MyMenu> myMenu;

MyLabel::MyLabel(std::shared_ptr<Font> fh, const std::string& text, int size,
		 const Color& selected, const Color& deselected) :
	Label(fh, "", size, true, true),
	m_color_selected(selected),
	m_color_deselected(deselected),
	m_font(fh), m_size(size)
{
	m_label = text;

	setText(m_label);
	deselect();
}

class AnimFadeAway : public Animation {
public:
	AnimFadeAway(bool vertical, float delta, float max, const Action &action) {
		m_delta = delta;
		m_max = max;
		m_vertical = vertical;
		m_action = action;
	}

	virtual ~AnimFadeAway() {}

protected:
	virtual void complete(Drawable *t) { m_action(); }
	virtual void nextFrame(Drawable *t);

private:
	bool m_vertical;
	float m_delta, m_max;
	Action m_action;
};

class AnimFadeIn : public Animation {
public:
	AnimFadeIn(bool vertical, float max, const Action &action) {
		m_max = max;
		m_vertical = vertical;
		m_action = action;
	}

	virtual ~AnimFadeIn() {}

protected:
	virtual void complete(Drawable *t) { m_action(); }
	virtual void nextFrame(Drawable *t);

private:
	bool m_vertical;
	float m_delta, m_max;
	Action m_action;
};

void PathLabel::activate()
{
	const std::string& name = getLabel();
	const fs::path& pwd = myMenu->pwd();
	bool back = name.compare("..") == 0;

	fs::path path = back ? pwd.parent_path() : pwd / name;

	if (!back && fs::is_regular_file(path)) {
		if (emu_check_cd(path.c_str())) {
			/* Launch ISO! */
			myMenu->startExit();
		} else {
			/* TODO: show error */
		}
	} else {
		myMenu->preparePopulate(path, back, false);
	}
}

void PathLabel::cancel()
{
	fs::path path = myMenu->pwd().parent_path();

	myMenu->preparePopulate(path, true,
				path == myMenu->pwd());
}

void MainMenuLabel::cancel()
{
	/* No action for cancel on the main menu */
}

MyMenu::MyMenu(std::shared_ptr<Font> fnt, const fs::path &path)
{
	m_bg = std::make_shared<Background>();

	m_bg->setTint(Color(1.0f, 0.7f, 0.7f, 0.7f));

	m_top_scene = std::make_shared<Scene>();
	m_scene->subAdd(m_bg);
	m_scene->subAdd(m_top_scene);

	m_top_scene->setTranslate(Vector(-MENU_OFF_X, MENU_OFF_Y, 10));

	m_color0 = Color(1, 1, 1, 1);
	m_color1 = Color(1, 0.7f, 0.7f, 0.7f);
	m_input_allowed = false;

	m_font = fnt;
	m_exited = false;

	populate_dft();
}

void MyMenu::addEntry(std::shared_ptr<MyLabel> entry)
{
	entry->setTranslate(Vector(0, m_font_size * m_entries.size(), 0));
	m_top_scene->subAdd(entry);

	if (m_entries.empty())
		entry->select();

	m_entries.push_back(entry);
}

void MyMenu::populate_dft()
{
	m_font_size = MENU_ENTRY_SIZE;

	std::shared_ptr<AnimFadeIn> anim;

	m_entries.clear();
	m_top_scene->animRemoveAll();
	m_top_scene->subRemoveAll();
	m_top_scene->setTranslate(Vector(800.0f, MENU_OFF_Y, 10));

	addEntry(std::make_shared<MainMenuLabel>(m_font, "Run CD-ROM", m_font_size,
						 [&] {
		if (emu_check_cd(nullptr)) {
			/* Launch CD-Rom! */
			startExit();
		} else {
			/* TODO: show error */
		}
	}));

	addEntry(std::make_shared<MainMenuLabel>(m_font, "Select CD image", m_font_size,
						 [&] {
		preparePopulate(fs::path(TOP_PATH), false, false);
	}));

	addEntry(std::make_shared<MainMenuLabel>(m_font, "Options", m_font_size,
						 [&] {
	}));

	addEntry(std::make_shared<MainMenuLabel>(m_font, "Credits", m_font_size,
						 [&] {
	}));

	addEntry(std::make_shared<MainMenuLabel>(m_font, "Quit", m_font_size,
						 [&] {
		this->m_exited = true;
		startExit();
	}));

	anim = std::make_shared<AnimFadeIn>(false, MENU_OFF_X, [&] {
		m_top_scene->animRemoveAll();
	});
	m_top_scene->animRemoveAll();
	m_top_scene->animAdd(anim);

	m_input_allowed = true;
	m_cursel = 0;
}

void MyMenu::populate(fs::path path, bool back)
{
	float dx = back ? 1.0f : -1.0f;
	std::shared_ptr<AnimFadeIn> anim;
	dirent_t *d;
	bool is_file;
	int fd;

	m_font_size = ENTRY_SIZE;

	m_entries.clear();
	m_top_scene->animRemoveAll();
	m_top_scene->subRemoveAll();
	m_top_scene->setTranslate(Vector(dx * -800.0f, MENU_OFF_Y, 10));

	fd = fs_open(path.c_str(), O_DIR);
	if (fd == -1) {
		fprintf(stderr, "Unable to open root directory: %s\n", path.c_str());
		path = m_path;
		fd = fs_open(path.c_str(), O_DIR);
	}

	while ((d = fs_readdir(fd))) {
		fs::path filepath = path / d->name;
		is_file = fs::is_regular_file(filepath);

		if (is_file) {
			const std::string& ext = filepath.extension();

			if (ext != ".iso"
			    && ext != ".cue"
			    && ext != ".ccd"
			    && ext != ".exe"
			    && ext != ".mds"
			    && (!WITH_CHD || ext != ".chd")
			    && ext != ".pbp") {
				continue;
			}
		} else if (path == TOP_PATH) {
			std::string name = d->name;

			if (name != "cd"
			    && name != "pc"
			    && name != "ide"
			    && name != "sd") {
				continue;
			}
		}

		addEntry(std::make_shared<PathLabel>(m_font, d->name,
						     is_file, m_font_size));
	}

	anim = std::make_shared<AnimFadeIn>(false, MENU_OFF_X, [&] {
		m_top_scene->animRemoveAll();
	});
	m_top_scene->animRemoveAll();
	m_top_scene->animAdd(anim);
	fs_close(fd);

	m_path = path;
	m_cursel = 0;
	m_input_allowed = true;
}

void MyMenu::preparePopulate(fs::path path, bool back, bool dft)
{
	float dx = back ? 1.0f : -1.0f;

	if (back || fs::is_directory(path) || path == fs::path(TOP_PATH)) {
		auto anim = std::make_shared<AnimFadeAway>(false, dx,
							   800.0f * dx, [=] {
			if (dft)
				populate_dft();
			else
				populate(path, back);
		});

		m_top_scene->animRemoveAll();
		m_top_scene->animAdd(anim);
		m_input_allowed = false;
	}
}

void MyMenu::setEntry(unsigned int entry) {
	int offset_y;

	m_entries[m_cursel]->deselect();
	m_cursel = entry;

	offset_y = MENU_OFF_Y + (int)entry * -(int)m_font_size;

	m_entries[entry]->select();
	m_top_scene->animRemoveAll();
	m_top_scene->animAdd(std::make_shared<LogXYMover>(MENU_OFF_X, offset_y));
}

void MyMenu::inputEvent(const Event & evt) {
	if(evt.type != Event::EvtKeypress)
		return;

	if (!m_input_allowed)
		return;

	switch(evt.key) {
	case Event::KeyUp:
		if (m_cursel > 0)
			setEntry(m_cursel - 1);
		break;

	case Event::KeyLeft:
		if (m_cursel > 0)
			setEntry(m_cursel > 5 ? m_cursel - 5 : 0);
		break;

	case Event::KeyDown:
		if (m_cursel + 1 < m_entries.size())
			setEntry(m_cursel + 1);

		break;
	case Event::KeyRight:
		if (m_cursel + 1 < m_entries.size()) {
			unsigned int entry;

			if (m_cursel + 6 < m_entries.size())
				entry = m_cursel + 5;
			else
				entry = m_entries.size() - 1;


			setEntry(entry);
		}
		break;
	case Event::KeyCancel:
		m_entries[m_cursel]->cancel();

		break;
	case Event::KeySelect:
		m_entries[m_cursel]->activate();

		break;
	default:
		printf("Unhandled Event Key\n");
		break;
	}
}

void MyMenu::startExit() {
	// Apply some expmovers to the options.

	for (unsigned int i = 0; i < m_entries.size(); i++) {
		auto m = std::make_shared<ExpXYMover>(0, 1.0f + 0.2f * (float)i, 0, 1200);
		m->triggerAdd(std::make_shared<Death>());
		m_entries[i]->animAdd(m);
	}

	auto f = std::make_shared<AlphaFader>(0.0f, -1.0f / 60.0f);
	m_bg->animAdd(f);

	GenericMenu::startExit();
}

void AnimFadeAway::nextFrame(Drawable *t) {
	Vector p = t->getTranslate();
	float delta_x, delta_y, max_x, max_y, value = m_vertical ? p.y : p.x;
	bool done = m_delta < 0 ? (value <= m_max) : (value >= m_max);

	if (m_vertical) {
		delta_x = 0.0f;
		delta_y = m_delta;
		max_x = p.x;
		max_y = m_max;
	} else {
		delta_x = m_delta;
		delta_y = 0.0f;
		max_x = m_max;
		max_y = p.y;
	}

	if (done) {
		t->setTranslate(Vector(max_x, max_y, p.z));
		complete(t);
		return;
	}

	// Move 1.15x of the distance each frame
	p += Vector(delta_x, delta_y, 0);
	t->setTranslate(p);
	m_delta *= 1.15f;
}

void AnimFadeIn::nextFrame(Drawable *t) {
	float delta, delta_x, delta_y, max_x, max_y;
	Vector p = t->getTranslate();

	if (m_vertical) {
		max_x = p.x;
		max_y = m_max;
		delta = m_max - p.y;
		delta_x = 0.0f;
		delta_y = delta;
	} else {
		max_x = m_max;
		max_y = p.y;
		delta = m_max - p.x;
		delta_x = delta;
		delta_y = 0.0f;
	}

	if (fabs(delta) < 1.0f) {
		t->setTranslate(Vector(max_x, max_y, p.z));
		complete(t);
	} else {
		// Move 1/8th of the distance each frame
		p += Vector(delta_x / 8.0f, delta_y / 8.0f, 0);

		t->setTranslate(p);
	}
}

extern "C" bool runMenu(void)
{
	bool exited;

	// Load a font
	auto fnt = std::make_shared<Font>("/rd/typewriter.txf");

	// Create a menu
	myMenu = std::make_shared<MyMenu>(fnt, fs::path(TOP_PATH));

	// Do the menu
	myMenu->doMenu();

	exited = myMenu->hasExited();

	// Ok, we're all done! The RefPtrs will take care of mem cleanup.
	myMenu = nullptr;

	return exited;
}
