/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Emulator's GUI
 *
 * Copyright (C) 2024 Paul Cercueil <paul@crapouillou.net>
 */

#ifndef GENMENU_H
#define GENMENU_H

#include <tsu/drawables/label.h>
#include <tsu/font.h>

#include <functional>
#include <filesystem>
#include <memory>
#include <string>

namespace fs = std::filesystem;

typedef std::function<void(void)> Action;

class Background;

class MyLabel : public Label {
public:
	MyLabel(std::shared_ptr<Font> fh, const std::string &text, int size,
		bool centered, const Color& selected, const Color& deselected);

	~MyLabel() {}

	const std::string &getLabel() { return m_label; }

	void select()
	{
		setTint(m_color_selected);
	}

	void deselect()
	{
		setTint(m_color_deselected);
	}

	unsigned int fontSize() const {
		return m_size;
	}

	virtual void activate() = 0;
	virtual void cancel() = 0;

	void draw(int list);

protected:
	Color m_color_selected, m_color_deselected;
	std::string m_label;
	std::shared_ptr<Font> m_font;
	unsigned int m_size;
};

class PathLabel : public MyLabel {
public:
	PathLabel(std::shared_ptr<Font> fh, const std::string& text, bool is_file, int size)
		: MyLabel(fh, text, size, true,
			  is_file ? Color(1.0f, 0.7f, 0.7f, 1.0f) : Color(1.0f, 1.0f, 1.0f, 1.0f),
			  is_file ? Color(1.0f, 0.3f, 0.3f, 1.0f) : Color(1.0f, 0.7f, 0.7f, 0.7f))
	{
	}

	~PathLabel() {}

	virtual void activate();
	virtual void cancel();
};

class MainMenuLabel : public MyLabel {
public:
	enum MainMenuAction {
		LOAD_CDROM,
		LOAD_CDIMAGE,
		OPTIONS,
		CREDITS,
	};

	MainMenuLabel(std::shared_ptr<Font> fh, const std::string& text, int size,
		      const Action& action)
		: MyLabel(fh, text, size, true,
			  Color(1.0f, 1.0f, 1.0f, 1.0f),
			  Color(1.0f, 0.7f, 0.7f, 0.7f)),
		m_action(action)
	{
	}

	~MainMenuLabel() {}

	virtual void activate() {
		m_action();
	}

	virtual void cancel();

private:
	Action m_action;
};

class MyMenu : public GenericMenu {
public:
	MyMenu(std::shared_ptr<Font> fnt, const fs::path &path);

	virtual ~MyMenu() {}

	void populate_dft();
	void populate(fs::path path, bool back);

	void preparePopulate(fs::path path, bool back, bool dft);

	void setEntry(unsigned int entry);

	const fs::path& pwd() const { return m_path; };

	void addEntry(std::shared_ptr<MyLabel> entry);

	bool hasExited() const {
		return m_exited;
	}

	virtual void inputEvent(const Event & evt);

	virtual void startExit();

private:
	bool m_input_allowed;
	Color m_color0, m_color1;
	std::vector<std::shared_ptr<MyLabel> > m_entries;
	fs::path m_path;
	unsigned int m_cursel;
	unsigned int m_font_size;
	unsigned int m_xoffset;
	std::shared_ptr<Font> m_font;
	bool m_exited;
	std::shared_ptr<Background> m_bg;

	std::shared_ptr<Scene> m_top_scene;
};


#endif /* GENMENU_H */
