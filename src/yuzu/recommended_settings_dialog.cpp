// SPDX-License-Identifier: GPL-3.0-or-later

#include <string>
#include <QFont>
#include <QFontMetrics>
#include <QMessageBox>
#include <QPushButton>
#include <QStringList>
#include "qt_common/config/shared_translation.h"
#include "yuzu/recommended_settings_dialog.h"

namespace {

// One "name: previous value → new value" line per change, worded as the settings dialog words
// them.
QStringList Describe(QWidget* parent,
                     const std::vector<Settings::RecommendedSettingChange>& changes,
                     const QString& on, const QString& off) {
    const auto titles = ConfigurationShared::InitializeTranslations(parent);
    const auto enums = ConfigurationShared::ComboboxEnumeration(parent);

    const auto title_of = [&](const Settings::BasicSetting& setting) {
        const auto it = titles->find(setting.Id());
        return it == titles->end() ? QString{} : it->second.first;
    };
    const auto text_of = [&](const Settings::BasicSetting& setting, const std::string& value) {
        if (setting.TypeId() == "bool") {
            return value == "true" ? on : off;
        }
        if (const auto it = enums->find(setting.EnumIndex()); it != enums->end()) {
            for (const auto& [id, text] : it->second) {
                if (std::to_string(id) == value) {
                    return text;
                }
            }
        }
        QString text = QString::fromStdString(value);
        if (setting.Specialization() & Settings::Specialization::Percentage) {
            text.append(QLatin1Char{'%'});
        }
        return text;
    };

    QStringList lines;
    for (const auto& change : changes) {
        QString title = title_of(*change.setting);
        if (title.isEmpty()) {
            // A checkbox paired with a value is named by the value's setting.
            for (const auto& [key, other] : Settings::values.linkage.by_key) {
                if (other->PairedSetting() == change.setting) {
                    title = title_of(*other);
                }
            }
        }
        if (title.isEmpty()) {
            title = QString::fromStdString(change.setting->GetLabel());
        }
        if (title.endsWith(QLatin1Char{':'})) {
            title.chop(1);
        }
        lines.append(QStringLiteral("%1: %2 \u2192 %3")
                         .arg(title, text_of(*change.setting, change.previous),
                              text_of(*change.setting, change.current)));
    }
    return lines;
}

void ShowChanges(QWidget* parent, const QString& title, const QString& text,
                 const QStringList& lines, const QString& footer) {
    QMessageBox box(QMessageBox::Information, title, text, QMessageBox::Ok, parent);
    // A long list goes behind the details button so the box stays on the screen.
    if (lines.size() <= 12) {
        // Rich text, so that a line is not wrapped in the middle of a value.
        QString list;
        for (const QString& line : lines) {
            list += QStringLiteral("<nobr>%1</nobr><br>").arg(line.toHtmlEscaped());
        }
        box.setInformativeText(list + QStringLiteral("<br>") + footer.toHtmlEscaped());
    } else {
        box.setInformativeText(footer);
        box.setDetailedText(lines.join(QLatin1Char{'\n'}));
    }
    box.exec();
}

} // namespace

bool RecommendedSettingsDialog::Ask(QWidget* parent, const QString& host_text) {
    QMessageBox box(parent);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(tr("Best settings for this PC"));
    box.setText(tr("Use the settings CheezyEmu recommends for this PC?"));
    box.setInformativeText(
        tr("%1\n\nThey set performance options, AMD FSR, frame generation and Ocarina of Time's "
           "render size to what fits this graphics card and memory. CheezyEmu keeps your own "
           "settings, so you can go back at any time with Help > Restore My Previous Settings. "
           "Settings you made for a single game (right-click a game > Configure Game) do not "
           "change and still win over these.")
            .arg(host_text));
    auto* const use = box.addButton(tr("Use recommended settings"), QMessageBox::AcceptRole);
    box.addButton(tr("Not now"), QMessageBox::RejectRole);
    box.setDefaultButton(use);
    // The theme draws the default button's text bold, but the button is sized for normal text:
    // the label was cut off ("Jse recommended setting").
    QFont bold = use->font();
    bold.setBold(true);
    use->setMinimumWidth(QFontMetrics{bold}.horizontalAdvance(use->text()) + 40);
    box.exec();
    return box.clickedButton() == use;
}

void RecommendedSettingsDialog::ShowApplied(
    QWidget* parent, const std::vector<Settings::RecommendedSettingChange>& changes) {
    if (changes.empty()) {
        QMessageBox::information(
            parent, tr("Recommended settings"),
            tr("Your settings already match the recommended settings. Nothing changed."));
        return;
    }
    ShowChanges(parent, tr("Recommended settings"), tr("CheezyEmu now uses the recommended settings."),
                Describe(parent, changes, tr("On"), tr("Off")),
                tr("To go back, use Help > Restore My Previous Settings."));
}

void RecommendedSettingsDialog::ShowRestored(
    QWidget* parent, const std::vector<Settings::RecommendedSettingChange>& changes) {
    if (changes.empty()) {
        QMessageBox::information(
            parent, tr("Previous settings"),
            tr("Your settings already match your previous settings. Nothing changed."));
        return;
    }
    ShowChanges(parent, tr("Previous settings"), tr("CheezyEmu uses your previous settings again."),
                Describe(parent, changes, tr("On"), tr("Off")),
                tr("To use the recommended settings again, use Help > Apply Recommended "
                   "Settings."));
}
