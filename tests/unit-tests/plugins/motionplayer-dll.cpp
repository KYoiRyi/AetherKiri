//
// Created to verify motionplayer/emoteplayer behavior aligned to libkrkr2.so.
//

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <filesystem>
#include <iostream>

#include "motionplayer/EmotePlayer.h"
#include "motionplayer/Player.h"
#include "motionplayer/ResourceManager.h"
#include "motionplayer/RuntimeSupport.h"
#include "psbfile/PSBValue.h"
#include "test_config.h"
#include "tjsObject.h"

namespace {

    constexpr tjs_int kEmoteSeed = 742877301;

    ttstr motionFixturePath() {
        return ttstr(TEST_FILES_PATH "/emote/e-mote3.0バニラパジャマa.psb");
    }

    ttstr pimgFixturePath() {
        return ttstr(TEST_FILES_PATH "/emote/ezsave.pimg");
    }

    void setEmoteSeed() {
        tTJSVariant seed{kEmoteSeed};
        tTJSVariant *params[] = { &seed };
        REQUIRE(motion::ResourceManager::setEmotePSBDecryptSeed(
                    nullptr, 1, params, nullptr) == TJS_S_OK);
    }

    tTJSVariant getProp(const tTJSVariant &object, const tjs_char *name) {
        REQUIRE(object.Type() == tvtObject);
        auto *dispatch = object.AsObjectNoAddRef();
        REQUIRE(dispatch != nullptr);

        tTJSVariant result;
        REQUIRE(TJS_SUCCEEDED(dispatch->PropGet(0, name, nullptr, &result,
                                               dispatch)));
        return result;
    }

    tTJSVariant getIndex(const tTJSVariant &object, tjs_int index) {
        REQUIRE(object.Type() == tvtObject);
        auto *dispatch = object.AsObjectNoAddRef();
        REQUIRE(dispatch != nullptr);

        tTJSVariant result;
        REQUIRE(TJS_SUCCEEDED(
            dispatch->PropGetByNum(TJS_IGNOREPROP, index, &result, dispatch)));
        return result;
    }

    tjs_int variantCount(const tTJSVariant &object) {
        return static_cast<tjs_int>(getProp(object, TJS_W("count")).AsInteger());
    }

    std::vector<std::pair<ttstr, tTJSVariant>>
    dictionaryEntries(const tTJSVariant &object) {
        struct Enumerator : tTJSDispatch {
            std::vector<std::pair<ttstr, tTJSVariant>> entries;

            tjs_error FuncCall(tjs_uint32, const tjs_char *, tjs_uint32 *,
                               tTJSVariant *result, tjs_int numparams,
                               tTJSVariant **param, iTJSDispatch2 *) override {
                if(numparams >= 3) {
                    entries.emplace_back(ttstr(*param[0]), *param[2]);
                }
                if(result) {
                    *result = static_cast<tjs_int>(1);
                }
                return TJS_S_OK;
            }
        } enumerator;

        REQUIRE(object.Type() == tvtObject);
        auto *dispatch = object.AsObjectNoAddRef();
        REQUIRE(dispatch != nullptr);
        tTJSVariantClosure closure(&enumerator, nullptr);
        if(TJS_FAILED(
               dispatch->EnumMembers(TJS_IGNOREPROP, &closure, dispatch))) {
            return {};
        }
        return enumerator.entries;
    }

    void dumpDictionary(const tTJSVariant &object, const std::string &prefix,
                        int depth = 0) {
        if(depth > 2 || object.Type() != tvtObject) {
            return;
        }

        for(const auto &[key, value] : dictionaryEntries(object)) {
            std::cerr << prefix << key.AsStdString()
                      << " type=" << static_cast<int>(value.Type());
            if(value.Type() == tvtString) {
                std::cerr << " value=" << ttstr(value).AsStdString();
            } else if(value.Type() == tvtInteger) {
                std::cerr << " value=" << value.AsInteger();
            } else if(value.Type() == tvtReal) {
                std::cerr << " value=" << value.AsReal();
            }
            std::cerr << "\n";

            if(value.Type() != tvtObject) {
                continue;
            }

            if(const auto count = variantCount(value); count > 0) {
                const auto limit = std::min<tjs_int>(count, 3);
                std::cerr << prefix << "  [count]=" << count << "\n";
                for(tjs_int index = 0; index < limit; ++index) {
                    const auto item = getIndex(value, index);
                    std::cerr << prefix << "  [" << index
                              << "] type=" << static_cast<int>(item.Type());
                    if(item.Type() == tvtString) {
                        std::cerr << " value=" << ttstr(item).AsStdString();
                    } else if(item.Type() == tvtInteger) {
                        std::cerr << " value=" << item.AsInteger();
                    } else if(item.Type() == tvtReal) {
                        std::cerr << " value=" << item.AsReal();
                    }
                    std::cerr << "\n";
                    if(item.Type() == tvtObject) {
                        dumpDictionary(item, prefix + "    ", depth + 1);
                    }
                }
            } else {
                dumpDictionary(value, prefix + "  ", depth + 1);
            }
        }
    }

    void dumpPsbValue(const std::shared_ptr<PSB::IPSBValue> &value,
                      const std::string &prefix, int depth = 0) {
        if(!value || depth > 3) {
            return;
        }

        if(auto text = std::dynamic_pointer_cast<PSB::PSBString>(value)) {
            std::cerr << prefix << "string=" << text->value << "\n";
            return;
        }
        if(auto number = std::dynamic_pointer_cast<PSB::PSBNumber>(value)) {
            std::cerr << prefix << "number=" << number->toString() << "\n";
            return;
        }
        if(auto boolean = std::dynamic_pointer_cast<PSB::PSBBool>(value)) {
            std::cerr << prefix << "bool=" << (boolean->value ? "true" : "false")
                      << "\n";
            return;
        }
        if(auto resource = std::dynamic_pointer_cast<PSB::PSBResource>(value)) {
            std::cerr << prefix << "resource index="
                      << resource->index.value_or(UINT32_MAX)
                      << " size=" << resource->data.size() << "\n";
            return;
        }
        if(auto list = std::dynamic_pointer_cast<PSB::PSBList>(value)) {
            std::cerr << prefix << "list size=" << list->size() << "\n";
            const auto limit = std::min<size_t>(list->size(), 3);
            for(size_t index = 0; index < limit; ++index) {
                std::cerr << prefix << "  [" << index << "]\n";
                dumpPsbValue((*list)[static_cast<int>(index)], prefix + "    ",
                             depth + 1);
            }
            return;
        }
        if(auto dic = std::dynamic_pointer_cast<PSB::PSBDictionary>(value)) {
            std::cerr << prefix << "dict size="
                      << std::distance(dic->begin(), dic->end()) << "\n";
            int count = 0;
            for(const auto &[key, child] : *dic) {
                std::cerr << prefix << "  " << key << "\n";
                dumpPsbValue(child, prefix + "    ", depth + 1);
                if(++count >= 12) {
                    break;
                }
            }
            return;
        }

        std::cerr << prefix << "type=" << static_cast<int>(value->getType())
                  << " text=" << value->toString() << "\n";
    }

    bool containsString(const tTJSVariant &object, const ttstr &expected) {
        const auto count = variantCount(object);
        for(tjs_int index = 0; index < count; ++index) {
            if(ttstr(getIndex(object, index)) == expected) {
                return true;
            }
        }
        return false;
    }

} // namespace

TEST_CASE("motionplayer resource chain and query surface") {
    setEmoteSeed();

    motion::Player player;
    const auto motionPath = motionFixturePath();
    const auto pimgPath = pimgFixturePath();

    REQUIRE_FALSE(player.isExistMotion(ttstr(TEST_FILES_PATH "/emote/missing.psb")));
    REQUIRE_FALSE(player.isExistMotion(pimgPath));
    REQUIRE(player.findMotion(pimgPath).Type() == tvtVoid);

    const auto motion = player.findMotion(motionPath);
    REQUIRE(motion.Type() == tvtObject);
    REQUIRE(player.isExistMotion(motionPath));

    const auto motions = player.motionList();
    REQUIRE(variantCount(motions) == 1);

    const auto layerNames = player.getLayerNames();
    REQUIRE(variantCount(layerNames) > 0);

    const auto firstLayer = ttstr(getIndex(layerNames, 0));
    REQUIRE_FALSE(firstLayer.IsEmpty());
    REQUIRE(player.getLayerMotion(firstLayer).Type() == tvtObject);
    REQUIRE(player.getLayerGetter(firstLayer).Type() == tvtObject);
    REQUIRE(variantCount(player.getLayerGetterList()) == variantCount(layerNames));

    const auto firstLayerId = player.requireLayerId(firstLayer);
    REQUIRE(firstLayerId > 0);
    player.releaseLayerId(firstLayerId);
    REQUIRE(player.requireLayerId(firstLayer) > 0);

    const auto mainTimelineLabels = player.getMainTimelineLabelList();
    const auto diffTimelineLabels = player.getDiffTimelineLabelList();
    REQUIRE(mainTimelineLabels.Type() == tvtObject);
    REQUIRE(diffTimelineLabels.Type() == tvtObject);

    if(variantCount(mainTimelineLabels) > 0) {
        const auto label = ttstr(getIndex(mainTimelineLabels, 0));
        REQUIRE_FALSE(label.IsEmpty());
        REQUIRE_FALSE(player.getTimelinePlaying(label));
        REQUIRE(player.getVariableFrameList(label).Type() == tvtObject);
    }

    const auto variableKeys = player.getVariableKeys();
    REQUIRE(variableKeys.Type() == tvtObject);
    if(variantCount(variableKeys) > 0) {
        const auto variableLabel = ttstr(getIndex(variableKeys, 0));
        REQUIRE(player.getVariableFrameList(variableLabel).Type() == tvtObject);
    }
}

TEST_CASE("motionplayer draw cache and playback state") {
    setEmoteSeed();

    motion::Player player;
    const auto motionPath = motionFixturePath();
    const auto pimgPath = pimgFixturePath();

    REQUIRE(player.findMotion(motionPath).Type() == tvtObject);
    REQUIRE(player.findSource(pimgPath).Type() == tvtObject);

    player.setFlip(true);
    player.setOpacity(0.5);
    player.setVisible(true);
    player.setSlant(1.25);
    player.setZoom(1.5);
    player.setClearColor(0x102030);
    player.registerBg(ttstr(TJS_W("bg")));
    player.registerCaption(ttstr(TJS_W("caption")));

    player.draw();
    const auto canvas = player.captureCanvas();
    REQUIRE(canvas.Type() == tvtObject);
    REQUIRE(getProp(canvas, TJS_W("width")).AsInteger() > 0);
    REQUIRE(getProp(canvas, TJS_W("height")).AsInteger() > 0);
    REQUIRE(getProp(canvas, TJS_W("sourceCount")).AsInteger() == 1);
    REQUIRE(getProp(canvas, TJS_W("backgroundCount")).AsInteger() == 1);
    REQUIRE(getProp(canvas, TJS_W("captionCount")).AsInteger() == 1);
    REQUIRE(getProp(canvas, TJS_W("flip")).AsInteger() == 1);
    REQUIRE(getProp(canvas, TJS_W("opacity")).AsReal() == 0.5);

    player.frameProgress(16.0);
    REQUIRE(player.getFrameLastTime() == 16.0);
    REQUIRE(player.getTickCount() == 16.0);
    REQUIRE(player.getFrameTickCount() == 1.0);

    player.clearCache();
    player.draw();
    REQUIRE(getProp(player.captureCanvas(), TJS_W("sourceCount")).AsInteger() ==
            0);

    REQUIRE(player.findSource(pimgPath).Type() == tvtObject);
    player.unload(pimgPath);
    player.draw();
    REQUIRE(getProp(player.captureCanvas(), TJS_W("sourceCount")).AsInteger() ==
            0);

    player.unloadAll();
    REQUIRE(variantCount(player.motionList()) == 0);
}

TEST_CASE("emoteplayer timeline state and todo stubs") {
    setEmoteSeed();

    motion::ResourceManager rm;
    const auto module = rm.load(motionFixturePath());
    REQUIRE(module.Type() == tvtObject);

    motion::EmotePlayer player(rm);
    player.setModule(module);
    REQUIRE(player.getModule().Type() == tvtObject);

    player.setCoord(100.0, 200.0);
    player.setScale(1.0);
    REQUIRE(player.contains(100.0, 200.0));
    REQUIRE_FALSE(player.contains(99.0, 199.0));

    player.hide();
    REQUIRE_FALSE(player.contains(100.0, 200.0));
    player.show();
    REQUIRE(player.contains(100.0, 200.0));

    player.setVariable(TJS_W("manual"), 3.5);
    REQUIRE(player.getVariable(TJS_W("manual")) == 3.5);

    // After delegation to Player, countVariables returns real count from PSB.
    // The loaded PSB may or may not have variables.
    const auto varCount = player.countVariables();
    REQUIRE(varCount >= 0);
    if(varCount > 0) {
        REQUIRE_FALSE(ttstr(player.getVariableLabelAt(0)).IsEmpty());
    }
    REQUIRE(player.getOuterForce().Type() == tvtVoid);

    const auto mainCount = player.countMainTimelines();
    const auto diffCount = player.countDiffTimelines();
    REQUIRE((mainCount + diffCount) > 0);

    const auto label =
        mainCount > 0 ? player.getMainTimelineLabelAt(0)
                      : player.getDiffTimelineLabelAt(0);
    REQUIRE_FALSE(label.IsEmpty());
    REQUIRE(player.getTimelineTotalFrameCount(label) >= 0);

    player.playTimeline(label, motion::TimelinePlayFlagParallel);
    REQUIRE(player.isTimelinePlaying(label));
    REQUIRE(player.getAnimating());
    REQUIRE(player.countPlayingTimelines() >= 1);
    REQUIRE(player.getPlayingTimelineLabelAt(0) == label);

    player.pass(10.0);
    REQUIRE(player.getProgress() == 10.0);

    player.fadeOutTimeline(label, 1.0, 0);
    REQUIRE_FALSE(player.isTimelinePlaying(label));
    REQUIRE(player.getTimelineBlendRatio(label) == 0.0);

    player.fadeInTimeline(label, 1.0, motion::TimelinePlayFlagSequential);
    REQUIRE(player.isTimelinePlaying(label));
    REQUIRE(player.getTimelineBlendRatio(label) == 1.0);

    player.skip();
    if(!player.isLoopTimeline(label)) {
        REQUIRE_FALSE(player.isTimelinePlaying(label));
    }

    player.playTimeline(label, motion::TimelinePlayFlagParallel);
    player.stopTimeline(TJS_W(""));
    REQUIRE_FALSE(player.getAnimating());

    player.assignState();
    player.setOuterForce(1.0, 2.0);
}

TEST_CASE("motionplayer can play internal logo motion clips") {
    setEmoteSeed();

    const auto baseDir = std::filesystem::path(".debugtmp") / "titleprobe_hd" /
        "data1080";
    if(!std::filesystem::exists(baseDir / "yuzulogo.mtn") ||
       !std::filesystem::exists(baseDir / "m2logo.mtn")) {
        return;
    }

    motion::Player player;
    const auto yuzuPath =
        ttstr(std::filesystem::absolute(baseDir / "yuzulogo.mtn").string());
    const auto m2Path =
        ttstr(std::filesystem::absolute(baseDir / "m2logo.mtn").string());

    const auto verifyOne = [&](const ttstr &path, const ttstr &label,
                               const tjs_int expectedLayers,
                               const tjs_int expectedFrames) {
        INFO("path=" << path.AsStdString() << " label=" << label.AsStdString());
        REQUIRE(player.findMotion(path).Type() == tvtObject);
        const auto snapshot = motion::detail::lookupModuleSnapshot(
            player.findMotion(path));
        REQUIRE(snapshot != nullptr);

        const auto mainLabels = player.getMainTimelineLabelList();
        const auto diffLabels = player.getDiffTimelineLabelList();
        REQUIRE(containsString(mainLabels, label));
        REQUIRE(variantCount(diffLabels) == 0);
        REQUIRE(player.getTimelineTotalFrameCount(label) == expectedFrames);

        player.playTimeline(label, motion::PlayFlagForce);
        REQUIRE(player.getTimelinePlaying(label));
        const auto layerNames = player.getLayerNames();
        const auto getterList = player.getLayerGetterList();
        const auto commands = player.getCommandList();
        std::cerr << "logo test path=" << path.AsStdString()
                  << " label=" << label.AsStdString()
                  << " layers=" << variantCount(layerNames)
                  << " commands=" << variantCount(commands) << "\n";
        for(tjs_int index = 0; index < variantCount(commands); ++index) {
            const auto command = ttstr(getIndex(commands, index));
            int sourceType = -1;
            try {
                sourceType = static_cast<int>(player.findSource(command).Type());
            } catch(...) {
                std::cerr << "  command[" << index << "]=" << command.AsStdString()
                          << " sourceError=<non-std-exception>\n";
                continue;
            }
            std::cerr << "  command[" << index << "]=" << command.AsStdString()
                      << " sourceType=" << sourceType << "\n";
        }
        for(tjs_int index = 0; index < variantCount(layerNames) && index < 2; ++index) {
            const auto layerName = ttstr(getIndex(layerNames, index));
            std::cerr << "  layer[" << index << "]=" << layerName.AsStdString()
                      << "\n";
            const auto clip =
                snapshot->clipsByLabel.find(label.AsStdString());
            REQUIRE(clip != snapshot->clipsByLabel.end());
            const auto layer =
                clip->second.layersByName.find(layerName.AsStdString());
            REQUIRE(layer != clip->second.layersByName.end());
            if(const auto frameList = (*layer->second)["frameList"]) {
                std::cerr << "    native frameList\n";
                dumpPsbValue(frameList, "      ");
            }
            if(const auto children = (*layer->second)["children"]) {
                std::cerr << "    native children\n";
                dumpPsbValue(children, "      ");
            }
        }
        REQUIRE(variantCount(layerNames) == expectedLayers);
        REQUIRE(variantCount(getterList) == expectedLayers);
        REQUIRE(player.getLayerMotion(ttstr(getIndex(player.getLayerNames(), 0)))
                    .Type() == tvtObject);
        REQUIRE(player.getProgressCompat() == Catch::Approx(0.0));

        player.frameProgress(static_cast<double>(expectedFrames - 1));
        REQUIRE(player.getTimelinePlaying(label));
        REQUIRE(player.getProgressCompat() < 1.0);

        player.frameProgress(1.0);
        REQUIRE_FALSE(player.getTimelinePlaying(label));
        REQUIRE(player.getProgressCompat() == Catch::Approx(1.0));
    };

    verifyOne(yuzuPath, TJS_W("yuzulogo"), 4, 241);
    verifyOne(m2Path, TJS_W("back_white"), 2, 91);
}
