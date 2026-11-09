#include "sfc/mlp.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>

namespace skg::sfc {

Mlp::Mlp(const MlpOptions &opt, int nFeatures, int nClasses)
    : opt_(opt), nFeatures_(nFeatures), nClasses_(nClasses), rng_(opt.seed) {
  std::vector<int> sizes{nFeatures};
  sizes.insert(sizes.end(), opt.hidden.begin(), opt.hidden.end());
  sizes.push_back(nClasses);
  for (size_t i = 1; i < sizes.size(); ++i) {
    Layer l;
    l.in = sizes[i - 1];
    l.out = sizes[i];
    // Glorot uniform, the bound scikit-learn uses for relu/tanh (logistic: x4).
    double bound = std::sqrt(6.0 / (l.in + l.out));
    if (opt.activation == QLatin1String("logistic"))
      bound *= 4;
    l.W.resize(size_t(l.in) * size_t(l.out));
    for (double &w : l.W)
      w = (rng_.uniform() * 2 - 1) * bound;
    l.b.assign(size_t(l.out), 0.0);
    for (double &bb : l.b)
      bb = (rng_.uniform() * 2 - 1) * bound;
    l.mW.assign(l.W.size(), 0.0);
    l.vW.assign(l.W.size(), 0.0);
    l.mb.assign(l.b.size(), 0.0);
    l.vb.assign(l.b.size(), 0.0);
    layers_.push_back(std::move(l));
  }
}

double Mlp::act(double v) const {
  if (opt_.activation == QLatin1String("tanh"))
    return std::tanh(v);
  if (opt_.activation == QLatin1String("logistic"))
    return 1.0 / (1.0 + std::exp(-v));
  return v > 0 ? v : 0.0;
}

double Mlp::actGrad(double a) const {
  if (opt_.activation == QLatin1String("tanh"))
    return 1.0 - a * a;
  if (opt_.activation == QLatin1String("logistic"))
    return a * (1.0 - a);
  return a > 0 ? 1.0 : 0.0;
}

void Mlp::forward(const double *x,
                  std::vector<std::vector<double>> &acts) const {
  acts.assign(layers_.size() + 1, {});
  acts[0].assign(x, x + nFeatures_);
  for (size_t li = 0; li < layers_.size(); ++li) {
    const Layer &l = layers_[li];
    std::vector<double> &out = acts[li + 1];
    out.assign(size_t(l.out), 0.0);
    for (int o = 0; o < l.out; ++o) {
      double s = l.b[size_t(o)];
      const double *w = l.W.data() + size_t(o) * size_t(l.in);
      for (int i = 0; i < l.in; ++i)
        s += w[i] * acts[li][size_t(i)];
      out[size_t(o)] = s;
    }
    if (li + 1 < layers_.size())
      for (double &v : out)
        v = act(v);
    else {
      // softmax
      const double mx = *std::max_element(out.begin(), out.end());
      double sum = 0;
      for (double &v : out) {
        v = std::exp(v - mx);
        sum += v;
      }
      for (double &v : out)
        v /= sum;
    }
  }
}

void Mlp::epoch(const std::vector<double> &X, const std::vector<int> &y) {
  const int n = int(y.size());
  if (n == 0)
    return;
  const int batch = std::clamp(opt_.batchSize, 1, n);
  const std::vector<int> order = rng_.permutation(n);
  std::vector<std::vector<double>> acts;
  std::vector<std::vector<double>> dW(layers_.size()), db(layers_.size());
  for (int start = 0; start < n; start += batch) {
    const int end = std::min(n, start + batch);
    const int m = end - start;
    for (size_t li = 0; li < layers_.size(); ++li) {
      dW[li].assign(layers_[li].W.size(), 0.0);
      db[li].assign(layers_[li].b.size(), 0.0);
    }
    for (int k = start; k < end; ++k) {
      const int r = order[size_t(k)];
      forward(X.data() + size_t(r) * size_t(nFeatures_), acts);
      // delta at the output: p - onehot
      std::vector<double> delta = acts.back();
      delta[size_t(y[size_t(r)])] -= 1.0;
      for (int li = int(layers_.size()) - 1; li >= 0; --li) {
        const Layer &l = layers_[size_t(li)];
        const std::vector<double> &in = acts[size_t(li)];
        for (int o = 0; o < l.out; ++o) {
          db[size_t(li)][size_t(o)] += delta[size_t(o)];
          double *g = dW[size_t(li)].data() + size_t(o) * size_t(l.in);
          for (int i = 0; i < l.in; ++i)
            g[i] += delta[size_t(o)] * in[size_t(i)];
        }
        if (li > 0) {
          std::vector<double> prev(size_t(l.in), 0.0);
          for (int i = 0; i < l.in; ++i) {
            double s = 0;
            for (int o = 0; o < l.out; ++o)
              s += l.W[size_t(o) * size_t(l.in) + size_t(i)] * delta[size_t(o)];
            prev[size_t(i)] = s * actGrad(in[size_t(i)]);
          }
          delta = std::move(prev);
        }
      }
    }
    ++steps_;
    const double lr = opt_.learningRate;
    for (size_t li = 0; li < layers_.size(); ++li) {
      Layer &l = layers_[li];
      auto update = [&](std::vector<double> &p, std::vector<double> &grad,
                        std::vector<double> &mo, std::vector<double> &ve,
                        bool penalise) {
        for (size_t i = 0; i < p.size(); ++i) {
          double g = grad[i] / m;
          if (penalise)
            g += opt_.alpha * p[i] / m;
          if (opt_.solver == QLatin1String("sgd")) {
            p[i] -= lr * g;
            continue;
          }
          mo[i] = opt_.beta1 * mo[i] + (1 - opt_.beta1) * g;
          ve[i] = opt_.beta2 * ve[i] + (1 - opt_.beta2) * g * g;
          const double mhat = mo[i] / (1 - std::pow(opt_.beta1, steps_));
          const double vhat = ve[i] / (1 - std::pow(opt_.beta2, steps_));
          p[i] -= lr * mhat / (std::sqrt(vhat) + opt_.epsilon);
        }
      };
      update(l.W, dW[li], l.mW, l.vW, true);
      update(l.b, db[li], l.mb, l.vb, false);
    }
  }
  ++epochs_;
}

std::vector<double> Mlp::proba(const double *X, int rows) const {
  std::vector<double> out;
  out.reserve(size_t(rows) * size_t(nClasses_));
  std::vector<std::vector<double>> acts;
  for (int r = 0; r < rows; ++r) {
    forward(X + size_t(r) * size_t(nFeatures_), acts);
    out.insert(out.end(), acts.back().begin(), acts.back().end());
  }
  return out;
}

QByteArray Mlp::toJson() const {
  QJsonArray layers;
  for (const Layer &l : layers_) {
    QJsonArray W, b;
    for (double v : l.W)
      W.append(v);
    for (double v : l.b)
      b.append(v);
    layers.append(
        QJsonObject{{"in", l.in}, {"out", l.out}, {"W", W}, {"b", b}});
  }
  QJsonArray hidden;
  for (int h : opt_.hidden)
    hidden.append(h);
  const QJsonObject o{{"n_features", nFeatures_},
                      {"n_classes", nClasses_},
                      {"activation", opt_.activation},
                      {"hidden", hidden},
                      {"epochs", epochs_},
                      {"layers", layers}};
  return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

Mlp Mlp::fromJson(const QByteArray &json) {
  const QJsonObject o = QJsonDocument::fromJson(json).object();
  Mlp m;
  m.nFeatures_ = o.value("n_features").toInt();
  m.nClasses_ = o.value("n_classes").toInt();
  m.opt_.activation = o.value("activation").toString();
  m.epochs_ = o.value("epochs").toInt();
  for (const QJsonValue &v : o.value("hidden").toArray())
    m.opt_.hidden.push_back(v.toInt());
  for (const QJsonValue &lv : o.value("layers").toArray()) {
    const QJsonObject lo = lv.toObject();
    Layer l;
    l.in = lo.value("in").toInt();
    l.out = lo.value("out").toInt();
    for (const QJsonValue &v : lo.value("W").toArray())
      l.W.push_back(v.toDouble());
    for (const QJsonValue &v : lo.value("b").toArray())
      l.b.push_back(v.toDouble());
    l.mW.assign(l.W.size(), 0.0);
    l.vW.assign(l.W.size(), 0.0);
    l.mb.assign(l.b.size(), 0.0);
    l.vb.assign(l.b.size(), 0.0);
    m.layers_.push_back(std::move(l));
  }
  return m;
}

} // namespace skg::sfc
