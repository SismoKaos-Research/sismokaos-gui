#include "modules/study/settinghelp.h"

#include <QCoreApplication>

#include "core/ui.h"
#include "study/runs.h"

namespace skg {

namespace {

struct Help {
  Q_DECLARE_TR_FUNCTIONS(skg::SettingHelp)
};

SettingInfo indicators(const QString &key) {
  if (key == QLatin1String("estimate_mc"))
    return {Help::tr("Estimate Mc for each case"),
            Help::tr("Estimates the completeness magnitude (the magnitude "
                     "above which the catalog is taken to hold every event) "
                     "separately for each case, by maximum curvature on the "
                     "events of its zone. The \"mc\" setting is then not "
                     "used. Needs at least 50 events in the zone.")};
  if (key == QLatin1String("mc"))
    return {Help::tr("Completeness magnitude Mc"),
            Help::tr("The magnitude above which the catalog is taken to "
                     "hold every event. Events below it do not enter the "
                     "b-value, the a-value and η. Used when Mc is not "
                     "estimated for each case.")};
  if (key == QLatin1String("mag_bin"))
    return {Help::tr("Magnitude bin"),
            Help::tr("The rounding of the catalog's magnitudes (0.1 for "
                     "AFAD). The b-value uses Mc minus half a bin.")};
  if (key == QLatin1String("window_months"))
    return {Help::tr("Window (months)"),
            Help::tr("Length of the sliding time window each indicator "
                     "value is computed on. The catalog events used start "
                     "one window before the case's span.")};
  if (key == QLatin1String("step_months"))
    return {Help::tr("Step (months)"),
            Help::tr("How far the window advances between two indicator "
                     "values. A smaller step gives more, more overlapping "
                     "windows.")};
  if (key == QLatin1String("half_gauss_sigma"))
    return {Help::tr("Half-Gauss σ"),
            Help::tr("Width of the weighting used for the second version "
                     "of the mean magnitude and the energy rate, in which "
                     "the weights rise towards the newest event of the "
                     "window.")};
  if (key == QLatin1String("ma_windows"))
    return {Help::tr("Moving averages"),
            Help::tr("Trailing averages of the indicators over these "
                     "numbers of windows, e.g. 7, 15, 23. One set of "
                     "figures per number.")};
  if (key == QLatin1String("bollinger"))
    return {Help::tr("Bollinger bands"),
            Help::tr("Also draws each indicator with a trailing mean and "
                     "bands at ± k standard deviations around it.")};
  if (key == QLatin1String("bb_window"))
    return {Help::tr("Bollinger window"),
            Help::tr("How many windows the trailing mean and standard "
                     "deviation of the Bollinger bands cover.")};
  if (key == QLatin1String("bb_k"))
    return {Help::tr("Bollinger k"),
            Help::tr("Width of the Bollinger bands, in standard deviations "
                     "on each side of the trailing mean.")};
  if (key == QLatin1String("homogenize"))
    return {Help::tr("Homogenise magnitudes"),
            Help::tr("Converts AFAD magnitudes before 2013-11-20 onto the "
                     "2014-2019 ML/Mw practice, with the conversion shipped "
                     "with the application. Without it a span that crosses "
                     "that date mixes two magnitude scales.")};
  if (key == QLatin1String("decluster"))
    return {Help::tr("Decluster"),
            Help::tr("Keeps only the Gardner-Knopoff mainshocks and single "
                     "events, i.e. removes aftershock and foreshock "
                     "sequences. The indicators then rest on far fewer "
                     "events.")};
  if (key == QLatin1String("figures"))
    return {Help::tr("Write figures"),
            Help::tr("Writes a PNG figure per indicator besides the window "
                     "table. The comparison needs only the table.")};
  if (key == QLatin1String("figure_dpi"))
    return {Help::tr("Figure resolution (dpi)"),
            Help::tr("Resolution of the PNG figures in dots per inch.")};
  if (key == QLatin1String("label_mag"))
    return {Help::tr("Risk label: magnitude"),
            Help::tr("The table's risk column is 1 for the windows that end "
                     "shortly before an earthquake of at least this "
                     "magnitude, and 0 otherwise. Default 6.0.")};
  if (key == QLatin1String("label_days"))
    return {Help::tr("Risk label: days before"),
            Help::tr("How many days before such an earthquake a window may "
                     "end and still get the risk label 1. Default 90.")};
  if (key == QLatin1String("figure_language"))
    return {Help::tr("Figure language"),
            Help::tr("Language of the titles and axis labels in the "
                     "figures: en or tr.")};
  return {};
}

SettingInfo features(const QString &key) {
  if (key == QLatin1String("config_json"))
    return {Help::tr("Features configuration"),
            Help::tr("A config.json saved on the Features page (window, "
                     "filters, chaos parameters, bands). Empty uses the "
                     "defaults. Load it with the button below; the station, "
                     "the folders and the calibration are set by the "
                     "study for each case.")};
  if (key == QLatin1String("channels"))
    return {Help::tr("Channels"),
            Help::tr("Which sensor of a station is read when its files hold "
                     "several: the first two letters of the channel code, "
                     "such as HH (broadband) or HN (accelerometer), with a "
                     "location code in front when needed (00.HH). Empty: the "
                     "only sensor there is; with several, the one with the "
                     "most data in the span, and the run says which it took. "
                     "Never two at once.")};
  if (key == QLatin1String("calibration catalog"))
    return {Help::tr("Calibration catalog"),
            Help::tr("The earthquake catalog the quiet periods for the "
                     "calibration are found with: the study's, unless the "
                     "configuration names one of its own, which then "
                     "overrides the study's for this recipe.")};
  if (key == QLatin1String("chaos parameters (calibration)"))
    return {Help::tr("Chaos parameters (calibration)"),
            Help::tr("The embedding delay, embedding dimension, mean "
                     "period and sample-entropy tolerances, estimated for "
                     "each station on its own quiet periods.")};
  return {};
}

SettingInfo classification(const QString &key) {
  if (key == QLatin1String("features_recipe"))
    return {Help::tr("Features recipe"),
            Help::tr("The features recipe whose feature table is "
                     "classified, chosen from the study's features recipes. "
                     "The classification of a case waits until that recipe "
                     "has run on it.")};
  if (key == QLatin1String("band"))
    return {Help::tr("Band"),
            Help::tr("For a features recipe with frequency bands: the name "
                     "of the band whose table is used. Empty uses the table "
                     "of the run without bands.")};
  if (key == QLatin1String("classes"))
    return {Help::tr("Classes (days before the event)"),
            Help::tr("Each class is a range of days before the event, e.g. "
                     "1-30, 31-90. The windows recorded on those days get "
                     "that class; the models learn to tell the classes "
                     "apart. At least two.")};
  if (key == QLatin1String("days_per_class"))
    return {Help::tr("Days per class"),
            Help::tr("At most this many days are taken from each class's "
                     "range, spread evenly over it (every day of a 30-day "
                     "range, every second day of a 60-day one at 30).")};
  if (key == QLatin1String("models"))
    return {Help::tr("Models"),
            Help::tr("What to train: logistic, svm_linear, svm_poly, "
                     "svm_rbf, ann, random_forest, gradient_boosting, "
                     "xgboost. Logistic regression and the SVMs are the "
                     "baseline in the comparison.")};
  if (key == QLatin1String("feature_columns"))
    return {Help::tr("Feature columns"),
            Help::tr("The columns of the feature table given to the "
                     "models. Empty uses every numeric column.")};
  if (key == QLatin1String("train_size"))
    return {Help::tr("Train share of days"),
            Help::tr("Share of each class's days used for training (0 to "
                     "1). Whole days go to one set, never single windows.")};
  if (key == QLatin1String("val_size"))
    return {Help::tr("Validation share of days"),
            Help::tr("Share of each class's days used to choose the "
                     "parameters and the best model. The rest after "
                     "training and validation is the test set the scores "
                     "are reported on.")};
  if (key == QLatin1String("scaler"))
    return {Help::tr("Scaler"),
            Help::tr("How feature values are brought to a common scale, "
                     "fitted on the training days: robust (median and "
                     "quartiles), standard (mean and standard deviation) "
                     "or minmax.")};
  if (key == QLatin1String("random_seed"))
    return {Help::tr("Random seed"),
            Help::tr("Starting value of the random numbers (the split of "
                     "days, the models). The same seed repeats the same "
                     "run.")};
  if (key == QLatin1String("cv_folds"))
    return {Help::tr("Cross-validation folds"),
            Help::tr("With 2 or more, parameters are tuned by k-fold "
                     "cross-validation over the training and validation "
                     "days instead of the fixed validation days. 0 or 1 "
                     "turns it off.")};
  if (key == QLatin1String("quick"))
    return {Help::tr("Quick"),
            Help::tr("Tries one small parameter setting per model instead "
                     "of the full grids. For a first look; the scores are "
                     "not tuned.")};
  if (key == QLatin1String("sfc_toml"))
    return {Help::tr("Other classification settings"),
            Help::tr("Anything else, in the layout of the Classification "
                     "page's project.toml (parameter grids, class weights). "
                     "The settings above take precedence.")};
  if (key == QLatin1String("min_day_hours"))
    return {Help::tr("Minimum hours per day"),
            Help::tr("A day with fewer hours of windows than this is not "
                     "used.")};
  if (key == QLatin1String("feature table (calibration)"))
    return {Help::tr("Feature table (calibration)"),
            Help::tr("The feature table comes from the features recipe, "
                     "whose chaos parameters were calibrated for each "
                     "station.")};
  return {};
}

} // namespace

QString SettingInfo::tooltip(const QString &key) const {
  QString text = help;
  if (!text.isEmpty())
    text += QLatin1Char(' ');
  text += Help::tr("In study.toml: %1.").arg(key);
  return ui::helpText(text);
}

SettingInfo settingInfo(const QString &tool, const QString &key) {
  SettingInfo info;
  if (tool == study::kToolIndicators)
    info = indicators(key);
  else if (tool == study::kToolFeatures)
    info = features(key);
  else if (tool == study::kToolClassification)
    info = classification(key);
  if (info.label.isEmpty())
    info.label = key;
  return info;
}

} // namespace skg
