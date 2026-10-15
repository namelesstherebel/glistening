// Optional MilkDrop mode: projectM 3.x renders into the default framebuffer
// (pbuffer); the Library learns which presets suit which energy.
#pragma once
#include "director.hpp"

#include <memory>
#include <string>
#include <vector>

#if GLISTENING_PROJECTM
#include <libprojectM/projectM.hpp>
#endif

namespace tv {

#if GLISTENING_PROJECTM
// projectM reports presets that fail to load; they are skipped and remembered.
struct ProjectM : projectM {
    using projectM::projectM;
    mutable std::vector<unsigned> failed;
    void presetSwitchFailedEvent(bool, unsigned index, const std::string &) const override { failed.push_back(index); }
};

class MilkDrop {
public:
    std::vector<std::string> names;
    std::unique_ptr<Library> lib;
    int cur = -1;
    std::vector<int> history;

    // Returns "" or why it is unavailable.
    std::string init(int w, int h, int fps, const std::string &libPath) {
        projectM::Settings st;
        st.meshX = 220, st.meshY = 125, st.fps = fps, st.textureSize = 1024;
        st.windowWidth = w, st.windowHeight = h;
        const char *dir = getenv("VIZ_PRESETS");
        st.presetURL = dir && *dir ? dir : "/usr/share/projectM/presets";
        st.titleFontURL = "/usr/share/projectM/fonts/Vera.ttf";
        st.menuFontURL = "/usr/share/projectM/fonts/VeraMono.ttf";
        st.datadir = "/usr/share/projectM";
        st.smoothPresetDuration = 3;
        st.presetDuration = 1000000;  // the director switches presets, not projectM
        st.hardcutEnabled = false, st.hardcutDuration = 20, st.hardcutSensitivity = 10;
        st.beatSensitivity = 1, st.aspectCorrection = true, st.easterEgg = 0;
        st.shuffleEnabled = true, st.softCutRatingsEnabled = false;
        pm_.reset(new ProjectM(st));
        pm_->projectM_resetGL(w, h);
        for (unsigned i = 0; i < pm_->getPlaylistSize(); i++) names.push_back(pm_->getPresetName(i));
        if (names.empty()) return "no presets in " + st.presetURL;
        lib.reset(new Library(names));
        lib->load(path_ = libPath);
        return "";
    }
    bool ready() const { return pm_ && !names.empty(); }
    void resize(int w, int h) { pm_->projectM_resetGL(w, h); }
    void pcm(const float *lr, int frames) { pm_->pcm()->addPCMfloat_2ch(lr, frames * 2); }
    void render() { pm_->renderFrame(); }
    void select(int i, bool hard, bool remember = true) {
        pm_->selectPreset(i, hard);
        lib->played(i);
        if (remember) history.push_back(i);
        if (history.size() > 100) history.erase(history.begin());
        cur = i;
    }
    // After a frame: presets that failed to load are flagged; true if the current one broke.
    bool checkFailed() {
        bool broke = !pm_->failed.empty();
        for (unsigned i : pm_->failed)
            if (i < lib->p.size()) lib->p[i].flag = -2;
        pm_->failed.clear();
        return broke;
    }
    void save() const {
        if (lib) lib->save(path_);
    }
    std::string name(int i) const { return i >= 0 && i < (int)names.size() ? names[i].substr(names[i].find_last_of('/') + 1) : ""; }

private:
    std::unique_ptr<ProjectM> pm_;
    std::string path_;
};
#else
class MilkDrop {  // built without projectM
public:
    std::vector<std::string> names;
    std::unique_ptr<Library> lib;
    int cur = -1;
    std::vector<int> history;
    std::string init(int, int, int, const std::string &) { return "built without projectM (make PROJECTM=1)"; }
    bool ready() const { return false; }
    void resize(int, int) {}
    void pcm(const float *, int) {}
    void render() {}
    void select(int, bool, bool = true) {}
    bool checkFailed() { return false; }
    void save() const {}
    std::string name(int) const { return ""; }
};
#endif

}  // namespace tv
