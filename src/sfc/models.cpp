#include "sfc/models.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTemporaryFile>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <stdexcept>

#include "svm.h"
#include "xgboost/c_api.h"

namespace skg::sfc {

std::vector<double> balancedWeights(const std::vector<int> &y, int nClasses) {
  std::vector<int> count(static_cast<size_t>(nClasses), 0);
  for (int v : y)
    ++count[static_cast<size_t>(v)];
  std::vector<double> w(y.size());
  for (size_t i = 0; i < y.size(); ++i)
    w[i] = double(y.size()) /
           (double(nClasses) * double(count[static_cast<size_t>(y[i])]));
  return w;
}

std::vector<int> Model::predict(const Matrix &m) const {
  const std::vector<double> p = proba(m);
  std::vector<int> out(static_cast<size_t>(m.rows));
  for (int r = 0; r < m.rows; ++r) {
    const double *row =
        p.data() + static_cast<size_t>(r) * static_cast<size_t>(nClasses);
    out[static_cast<size_t>(r)] =
        int(std::max_element(row, row + nClasses) - row);
  }
  return out;
}

QByteArray Model::save() const {
  QJsonObject o{{"name", name},
                {"n_classes", nClasses},
                {"params", QJsonObject::fromVariantMap(params)},
                {"model", QString::fromLatin1(serialize().toBase64())}};
  return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

bool Model::saveTo(const QString &path) const {
  QSaveFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
    return false;
  f.write(save());
  return f.commit();
}

std::unique_ptr<Model> loadModel(const QString &path, const Settings &s) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly))
    throw std::runtime_error(("cannot read " + path).toStdString());
  const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
  if (!o.contains("name"))
    throw std::runtime_error((path + " is not a saved model").toStdString());
  auto m = makeModel(o.value("name").toString(),
                     o.value("params").toObject().toVariantMap(), s);
  m->nClasses = o.value("n_classes").toInt();
  m->deserialize(
      QByteArray::fromBase64(o.value("model").toString().toLatin1()));
  return m;
}

// --- XGBoost
// -------------------------------------------------------------------------

namespace {
void check(int rc, const char *what) {
  if (rc != 0)
    throw std::runtime_error(std::string(what) + ": " + XGBGetLastError());
}

struct DMatrix {
  DMatrixHandle h = nullptr;
  DMatrix(const Matrix &m, const std::vector<double> &weights,
          bool withLabels) {
    std::vector<float> x(m.X.begin(), m.X.end());
    check(XGDMatrixCreateFromMat(x.data(), bst_ulong(m.rows), bst_ulong(m.cols),
                                 std::numeric_limits<float>::quiet_NaN(), &h),
          "XGDMatrixCreateFromMat");
    if (withLabels) {
      std::vector<float> y(m.y.begin(), m.y.end());
      check(XGDMatrixSetFloatInfo(h, "label", y.data(), bst_ulong(m.rows)),
            "label");
    }
    if (!weights.empty()) {
      std::vector<float> w(weights.begin(), weights.end());
      check(XGDMatrixSetFloatInfo(h, "weight", w.data(), bst_ulong(m.rows)),
            "weight");
    }
  }
  ~DMatrix() {
    if (h)
      XGDMatrixFree(h);
  }
};

QString num(const QVariant &v) {
  return QString::number(v.toDouble(), 'g', 10);
}
} // namespace

XgbModel::XgbModel(const QString &kind, const QVariantMap &p, int seed,
                   int threads)
    : kind_(kind), seed_(seed), threads_(threads) {
  params = p;
}

XgbModel::~XgbModel() {
  if (booster_)
    XGBoosterFree(booster_);
}

void XgbModel::setParams(void *b, int nFeatures) const {
  auto set = [&](const char *k, const QString &v) {
    check(XGBoosterSetParam(b, k, v.toUtf8().constData()), k);
  };
  set("verbosity", "0");
  set("seed", QString::number(seed_));
  set("nthread", QString::number(threads_ > 0 ? threads_ : 0));
  set("objective", nClasses > 2 ? "multi:softprob" : "binary:logistic");
  if (nClasses > 2)
    set("num_class", QString::number(nClasses));
  if (kind_ == QLatin1String("logistic")) {
    // scikit-learn's L2 logistic regression: lambda = 1 / C on the summed loss.
    set("booster", "gblinear");
    set("updater", "coord_descent");
    set("feature_selector", "cyclic");
    set("lambda", num(1.0 / std::max(1e-9, params.value("C", 1.0).toDouble())));
    set("alpha", "0");
    set("eta", "0.5");
    return;
  }
  set("booster", "gbtree");
  if (kind_ == QLatin1String("random_forest")) {
    // One round of many parallel trees on bootstrap-like subsamples with
    // sqrt(p) features per split, as RandomForestClassifier.
    set("tree_method", "hist");
    set("num_parallel_tree",
        QString::number(params.value("n_estimators", 100).toInt()));
    set("subsample", "0.632");
    set("colsample_bynode",
        num(std::min(1.0, std::sqrt(double(nFeatures)) / double(nFeatures))));
    set("eta", "1");
    set("lambda", "0");
    const QVariant depth = params.value("max_depth");
    set("max_depth", depth.isValid() && depth.toInt() > 0
                         ? QString::number(depth.toInt())
                         : "0");
    set("grow_policy",
        depth.isValid() && depth.toInt() > 0 ? "depthwise" : "lossguide");
    set("max_leaves", depth.isValid() && depth.toInt() > 0 ? "0" : "0");
    set("min_child_weight",
        num(params.value("min_samples_leaf", 1).toDouble()));
    return;
  }
  if (kind_ == QLatin1String("gradient_boosting")) {
    // GradientBoostingClassifier: exact greedy trees, no L2, depth 3 by
    // default.
    set("tree_method", "exact");
    set("eta", num(params.value("learning_rate", 0.1)));
    set("max_depth", QString::number(params.value("max_depth", 3).toInt()));
    set("lambda", "0");
    set("min_child_weight",
        num(params.value("min_samples_leaf", 1).toDouble()));
    return;
  }
  // xgboost itself, XGBClassifier's defaults
  set("tree_method", "hist");
  set("eta", num(params.value("learning_rate", 0.3)));
  set("max_depth", QString::number(params.value("max_depth", 6).toInt()));
  if (params.contains("subsample"))
    set("subsample", num(params.value("subsample")));
  if (params.contains("colsample_bytree"))
    set("colsample_bytree", num(params.value("colsample_bytree")));
  if (params.contains("min_child_weight"))
    set("min_child_weight", num(params.value("min_child_weight")));
}

void XgbModel::fit(const Matrix &m, const std::vector<double> &weights) {
  if (booster_) {
    XGBoosterFree(booster_);
    booster_ = nullptr;
  }
  if (nClasses == 0)
    nClasses = *std::max_element(m.y.begin(), m.y.end()) + 1;
  DMatrix d(m, weights, true);
  check(XGBoosterCreate(&d.h, 1, &booster_), "XGBoosterCreate");
  setParams(booster_, m.cols);
  if (kind_ == QLatin1String("random_forest"))
    rounds_ = 1;
  else if (kind_ == QLatin1String("logistic"))
    rounds_ = std::max(1, params.value("max_iter", 200).toInt() / 10);
  else
    rounds_ = params.value("n_estimators", 100).toInt();
  for (int it = 0; it < rounds_; ++it)
    check(XGBoosterUpdateOneIter(booster_, it, d.h), "XGBoosterUpdateOneIter");
}

std::vector<double> XgbModel::proba(const Matrix &m) const {
  if (!booster_)
    throw std::runtime_error("model not trained");
  DMatrix d(m, {}, false);
  bst_ulong len = 0;
  const float *out = nullptr;
  check(XGBoosterPredict(booster_, d.h, 0, 0, 0, &len, &out),
        "XGBoosterPredict");
  std::vector<double> p;
  p.reserve(static_cast<size_t>(m.rows) * static_cast<size_t>(nClasses));
  if (nClasses > 2) {
    p.assign(out, out + len);
  } else
    for (bst_ulong i = 0; i < len; ++i) {
      p.push_back(1.0 - out[i]);
      p.push_back(out[i]);
    }
  return p;
}

QByteArray XgbModel::serialize() const {
  if (!booster_)
    return {};
  bst_ulong len = 0;
  const char *buf = nullptr;
  check(
      XGBoosterSaveModelToBuffer(booster_, R"({"format": "json"})", &len, &buf),
      "XGBoosterSaveModelToBuffer");
  return QByteArray(buf, int(len));
}

void XgbModel::deserialize(const QByteArray &data) {
  if (booster_)
    XGBoosterFree(booster_);
  booster_ = nullptr;
  check(XGBoosterCreate(nullptr, 0, &booster_), "XGBoosterCreate");
  check(XGBoosterLoadModelFromBuffer(booster_, data.constData(),
                                     bst_ulong(data.size())),
        "XGBoosterLoadModelFromBuffer");
}

// --- libsvm
// -------------------------------------------------------------------------

struct SvmModel::Impl {
  svm_model *model = nullptr;
  std::vector<std::vector<svm_node>>
      nodes; // training data kept alive for the model
  ~Impl() {
    if (model)
      svm_free_and_destroy_model(&model);
  }
};

namespace {
std::vector<svm_node> toNodes(const double *row, int cols) {
  std::vector<svm_node> n(static_cast<size_t>(cols) + 1);
  for (int c = 0; c < cols; ++c)
    n[static_cast<size_t>(c)] = {c + 1, row[c]};
  n[static_cast<size_t>(cols)] = {-1, 0};
  return n;
}
void quiet(const char *) {}
} // namespace

SvmModel::SvmModel(const QString &kernel, const QVariantMap &p,
                   std::optional<QString> classWeight, int seed)
    : kernel_(kernel), classWeight_(std::move(classWeight)), seed_(seed),
      impl_(std::make_unique<Impl>()) {
  params = p;
}

SvmModel::~SvmModel() = default;

void SvmModel::fit(const Matrix &m, const std::vector<double> &) {
  if (nClasses == 0)
    nClasses = *std::max_element(m.y.begin(), m.y.end()) + 1;
  impl_ = std::make_unique<Impl>();
  impl_->nodes.resize(static_cast<size_t>(m.rows));
  std::vector<svm_node *> rows(static_cast<size_t>(m.rows));
  std::vector<double> labels(m.y.begin(), m.y.end());
  for (int r = 0; r < m.rows; ++r) {
    impl_->nodes[static_cast<size_t>(r)] = toNodes(m.row(r), m.cols);
    rows[static_cast<size_t>(r)] = impl_->nodes[static_cast<size_t>(r)].data();
  }
  svm_problem prob{m.rows, labels.data(), rows.data()};
  svm_parameter p{};
  p.svm_type = C_SVC;
  p.kernel_type = kernel_ == QLatin1String("linear") ? LINEAR
                  : kernel_ == QLatin1String("poly") ? POLY
                                                     : RBF;
  p.degree = params.value("degree", 3).toInt();
  p.coef0 = params.value("coef0", 0.0).toDouble();
  // gamma: "scale" = 1 / (n_features * var(X)), "auto" = 1 / n_features
  const QVariant g = params.value("gamma", QStringLiteral("scale"));
  if (g.typeId() == QMetaType::QString) {
    double var = 0, mean = 0;
    for (double v : m.X)
      mean += v;
    mean /= double(m.X.size());
    for (double v : m.X)
      var += (v - mean) * (v - mean);
    var /= double(m.X.size());
    p.gamma = g.toString() == QLatin1String("auto") || var <= 0
                  ? 1.0 / m.cols
                  : 1.0 / (m.cols * var);
  } else
    p.gamma = g.toDouble();
  p.C = params.value("C", 1.0).toDouble();
  p.cache_size = 200;
  p.eps = 1e-3;
  p.shrinking = 1;
  p.probability = 1;
  std::vector<int> wl;
  std::vector<double> ww;
  if (classWeight_ && *classWeight_ == QLatin1String("balanced")) {
    std::vector<int> count(static_cast<size_t>(nClasses), 0);
    for (int v : m.y)
      ++count[static_cast<size_t>(v)];
    for (int c = 0; c < nClasses; ++c)
      if (count[static_cast<size_t>(c)]) {
        wl.push_back(c);
        ww.push_back(double(m.rows) / (double(nClasses) *
                                       double(count[static_cast<size_t>(c)])));
      }
    p.nr_weight = int(wl.size());
    p.weight_label = wl.data();
    p.weight = ww.data();
  }
  if (const char *err = svm_check_parameter(&prob, &p))
    throw std::runtime_error(std::string("libsvm: ") + err);
  svm_set_print_string_function(quiet);
  std::srand(unsigned(seed_)); // libsvm's probability fit uses rand()
  impl_->model = svm_train(&prob, &p);
  if (!impl_->model)
    throw std::runtime_error("libsvm: training failed");
}

std::vector<double> SvmModel::proba(const Matrix &m) const {
  if (!impl_->model)
    throw std::runtime_error("model not trained");
  const int k = svm_get_nr_class(impl_->model);
  std::vector<int> labels(static_cast<size_t>(k));
  svm_get_labels(impl_->model, labels.data());
  std::vector<double> out(
      static_cast<size_t>(m.rows) * static_cast<size_t>(nClasses), 0.0);
  std::vector<double> est(static_cast<size_t>(k));
  for (int r = 0; r < m.rows; ++r) {
    const std::vector<svm_node> n = toNodes(m.row(r), m.cols);
    svm_predict_probability(impl_->model, n.data(), est.data());
    for (int i = 0; i < k; ++i)
      if (labels[static_cast<size_t>(i)] >= 0 &&
          labels[static_cast<size_t>(i)] < nClasses)
        out[static_cast<size_t>(r) * static_cast<size_t>(nClasses) +
            static_cast<size_t>(labels[size_t(i)])] =
            est[static_cast<size_t>(i)];
  }
  return out;
}

std::vector<int> SvmModel::predict(const Matrix &m) const {
  if (!impl_->model)
    throw std::runtime_error("model not trained");
  std::vector<int> out(static_cast<size_t>(m.rows));
  for (int r = 0; r < m.rows; ++r) {
    const std::vector<svm_node> n = toNodes(m.row(r), m.cols);
    out[static_cast<size_t>(r)] = int(svm_predict(impl_->model, n.data()));
  }
  return out;
}

QByteArray SvmModel::serialize() const {
  if (!impl_->model)
    return {};
  QTemporaryFile tmp;
  if (!tmp.open())
    return {};
  tmp.close();
  if (svm_save_model(tmp.fileName().toLocal8Bit().constData(), impl_->model) !=
      0)
    return {};
  QFile f(tmp.fileName());
  if (!f.open(QIODevice::ReadOnly))
    return {};
  return f.readAll();
}

void SvmModel::deserialize(const QByteArray &data) {
  QTemporaryFile tmp;
  if (!tmp.open())
    throw std::runtime_error("cannot write a temporary file");
  tmp.write(data);
  tmp.close();
  impl_ = std::make_unique<Impl>();
  impl_->model = svm_load_model(tmp.fileName().toLocal8Bit().constData());
  if (!impl_->model)
    throw std::runtime_error("libsvm: cannot read the model");
}

// --- MLP
// -------------------------------------------------------------------------

MlpModel::MlpModel(const MlpOptions &opt) : opt_(opt) {}

void MlpModel::reset(int nFeatures, int nClasses_) {
  nClasses = nClasses_;
  net_ = Mlp(opt_, nFeatures, nClasses_);
}

void MlpModel::epoch(const Matrix &m) {
  if (net_.nFeatures() == 0)
    reset(m.cols,
          nClasses ? nClasses : *std::max_element(m.y.begin(), m.y.end()) + 1);
  net_.epoch(m.X, m.y);
}

void MlpModel::fit(const Matrix &m, const std::vector<double> &) {
  reset(m.cols,
        nClasses ? nClasses : *std::max_element(m.y.begin(), m.y.end()) + 1);
  for (int e = 0; e < std::max(1, epochs); ++e)
    net_.epoch(m.X, m.y);
}

std::vector<double> MlpModel::proba(const Matrix &m) const {
  return net_.proba(m.X.data(), m.rows);
}

QByteArray MlpModel::serialize() const { return net_.toJson(); }

void MlpModel::deserialize(const QByteArray &data) {
  net_ = Mlp::fromJson(data);
  nClasses = net_.nClasses();
}

MlpModel MlpModel::clone() const {
  MlpModel c(opt_);
  c.name = name;
  c.params = params;
  c.nClasses = nClasses;
  c.epochs = epochs;
  c.net_ = net_;
  return c;
}

MlpOptions mlpOptions(const QVariantMap &params, const Settings &s,
                      int nTrain) {
  MlpOptions o;
  o.hidden.clear();
  const QVariant h = params.value("hidden_layer_sizes", QVariantList{16});
  if (h.typeId() == QMetaType::QVariantList)
    for (const QVariant &v : h.toList())
      o.hidden.push_back(v.toInt());
  else
    o.hidden.push_back(h.toInt());
  o.activation = s.annFixed.value("activation", "relu").toString();
  o.solver = s.annFixed.value("solver", "adam").toString();
  const QVariant bs = params.value("batch_size", 256);
  o.batchSize = bs.isNull() || bs.toString() == QLatin1String("none")
                    ? nTrain
                    : bs.toInt();
  o.alpha = params.value("alpha", 1e-4).toDouble();
  o.learningRate = params.value("learning_rate_init", 1e-3).toDouble();
  o.seed = quint64(s.randomSeed);
  return o;
}

// --- factory
// -------------------------------------------------------------------------

std::unique_ptr<Model> makeModel(const QString &name, const QVariantMap &params,
                                 const Settings &s) {
  std::unique_ptr<Model> m;
  if (name == QLatin1String("logistic")) {
    QVariantMap p = params;
    p.insert("max_iter", s.logisticMaxIter);
    m = std::make_unique<XgbModel>(name, p, s.randomSeed);
    m->params = params;
  } else if (name == QLatin1String("random_forest") ||
             name == QLatin1String("gradient_boosting") ||
             name == QLatin1String("xgboost"))
    m = std::make_unique<XgbModel>(name, params, s.randomSeed);
  else if (name.startsWith(QLatin1String("svm_")))
    m = std::make_unique<SvmModel>(name.mid(4), params, s.classWeight,
                                   s.randomSeed);
  else if (name == QLatin1String("ann")) {
    m = std::make_unique<MlpModel>(mlpOptions(params, s, 0));
    m->params = params;
  } else
    throw std::invalid_argument(("unknown model: " + name).toStdString());
  m->name = name;
  return m;
}

} // namespace skg::sfc
