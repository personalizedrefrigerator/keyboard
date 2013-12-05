/*
 * This file is part of Maliit Plugins
 *
 * Copyright (C) 2011 Nokia Corporation and/or its subsidiary(-ies). All rights reserved.
 *
 * Contact: Mohammad Anwari <Mohammad.Anwari@nokia.com>
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice, this list
 * of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice, this list
 * of conditions and the following disclaimer in the documentation and/or other materials
 * provided with the distribution.
 * Neither the name of Nokia Corporation nor the names of its contributors may be
 * used to endorse or promote products derived from this software without specific
 * prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "wordengine.h"

namespace MaliitKeyboard {
namespace Logic {

namespace {

void appendToCandidates(WordCandidateList *candidates,
                        WordCandidate::Source source,
                        const QString &candidate,
                        bool is_preedit_capitalized)
{
    if (not candidates) {
        return;
    }

    QString changed_candidate(candidate);

    if (not changed_candidate.isEmpty() && is_preedit_capitalized) {
        changed_candidate[0] = changed_candidate.at(0).toUpper();
    }

    WordCandidate word_candidate(source, changed_candidate);

    if (not candidates->contains(word_candidate)) {
        candidates->append(word_candidate);
    }
}

} // namespace

#define DEFAULT_PLUGIN "libenglishplugin.so"

//! \class WordEngine
//! \brief Provides error correction (based on Hunspell) and word
//! prediction (based on Presage).

class WordEnginePrivate
{
public:

    bool use_predictive_text;

    bool use_spell_checker;

    LanguagePluginInterface* languagePlugin;

    explicit WordEnginePrivate();

    QString currentPlugin;
    void loadPlugin(QString pluginName)
    {
        if (pluginName == currentPlugin)
            return;

        QDir pluginsDir("/usr/share/maliit/plugins/com/ubuntu/lib/");

        QPluginLoader pluginLoader(pluginsDir.absoluteFilePath(pluginName));
        QObject *plugin = pluginLoader.instance();
        qDebug() << plugin << pluginName << pluginsDir.absoluteFilePath(pluginName);
        if (plugin) {
            languagePlugin = qobject_cast<LanguagePluginInterface *>(plugin);
            if (!languagePlugin) {
                qCritical() << "loading plugin failed: " + pluginName;

                // fallback
                if (pluginName != DEFAULT_PLUGIN)
                    loadPlugin(DEFAULT_PLUGIN);
            } else {
                qDebug() << "plugin" << pluginName << "loaded";
                currentPlugin = pluginName;
            }
        }
    }
};

WordEnginePrivate::WordEnginePrivate()
    : use_predictive_text(false)
    , use_spell_checker(false)
    , languagePlugin(0)
{
    loadPlugin(DEFAULT_PLUGIN);
}


//! \brief Constructor.
//! \param parent The owner of this instance. Can be 0, in case QObject
//!               ownership is not required.
WordEngine::WordEngine(QObject *parent)
    : AbstractWordEngine(parent)
    , d_ptr(new WordEnginePrivate)
{}

//! \brief Destructor.
WordEngine::~WordEngine()
{}

//! \brief WordEngine::isEnabled returns if the word engine is functional and enabled
//! \return
bool WordEngine::isEnabled() const
{
    Q_D(const WordEngine);
    return (AbstractWordEngine::isEnabled() &&
            (d->use_predictive_text || d->languagePlugin->spellCheckerEnabled()));
}

void WordEngine::setWordPredictionEnabled(bool enabled)
{
    Q_D(WordEngine);

    // Don't allow to enable word engine if no backends are available:
    if (!d->languagePlugin && enabled) {
        qWarning() << __PRETTY_FUNCTION__
                   << "No backend available, cannot enable word engine!";
        enabled = false;
    }

    if (enabled == d->use_predictive_text)
        return;

    bool totalEnabled = isEnabled();

    d->use_predictive_text = enabled;

    if(totalEnabled != isEnabled())
        Q_EMIT enabledChanged(isEnabled());
}

//! \brief WordEngine::enableSpellcheker turns on/off the usage of the spellchecker
//! \param enabled
void WordEngine::setSpellcheckerEnabled(bool enabled)
{
    Q_D(WordEngine);
    bool totalEnabled = isEnabled();

    d->use_spell_checker = enabled;

    d->languagePlugin->setSpellCheckerEnabled(d->use_spell_checker);
    if(totalEnabled != isEnabled())
        Q_EMIT enabledChanged(isEnabled());
}

void WordEngine::onWordCandidateSelected(QString word)
{
    Q_D(WordEngine);

    d->languagePlugin->wordCandidateSelected(word);
}

WordCandidateList WordEngine::fetchCandidates(Model::Text *text)
{
    Q_D(WordEngine);

    WordCandidateList candidates;
    const QString &preedit(text->preedit());
    const bool is_preedit_capitalized(not preedit.isEmpty() && preedit.at(0).isUpper());

    if (d->use_predictive_text) {

        d->languagePlugin->parse(text->surroundingLeft(), preedit);
        const QStringList suggestions = d->languagePlugin->getWordCandidates();

        Q_FOREACH(const QString &suggestion, suggestions) {
            appendToCandidates(&candidates, WordCandidate::SourcePrediction, suggestion, is_preedit_capitalized);
        }
    }

    // spell checking
    const bool correct_spelling(d->languagePlugin->spell(preedit));

    if (candidates.isEmpty() and not correct_spelling) {
        Q_FOREACH(const QString &correction, d->languagePlugin->spellCheckerSuggest(preedit, 5)) {
            appendToCandidates(&candidates, WordCandidate::SourceSpellChecking, correction, is_preedit_capitalized);
        }
    }

    text->setPreeditFace(candidates.isEmpty() ? (correct_spelling ? Model::Text::PreeditDefault
                                                                  : Model::Text::PreeditNoCandidates)
                                              : Model::Text::PreeditActive);

    text->setPrimaryCandidate(candidates.isEmpty() ? QString()
                                                   : candidates.first().label());


    return candidates;
}

void WordEngine::addToUserDictionary(const QString &word)
{
    Q_D(WordEngine);
    d->languagePlugin->addToSpellCheckerUserWordList(word);
}

void WordEngine::onLanguageChanged(const QString &languageId)
{
    Q_D(WordEngine);

    if (languageId == "zh")
        d->loadPlugin("libpinyinplugin.so");
    else
        d->loadPlugin(DEFAULT_PLUGIN);

    bool ok = d->languagePlugin->setSpellCheckerLanguage(languageId);
    if (ok)
        d->languagePlugin->setSpellCheckerEnabled(d->use_spell_checker);
}

AbstractLanguageFeatures* WordEngine::languageFeature()
{
    Q_D(WordEngine);
    return d->languagePlugin->languageFeature();
}

}} // namespace Logic, MaliitKeyboard
