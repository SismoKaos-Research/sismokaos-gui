// The per-window features: statistical, spectral and nonlinear (chaotic).
// Ports of sismokaos-cli's features/{math,cross,chaos}.rs, which are in turn
// bit-for-bit ports of Sismokaos-featureExtract's chaos_algorithms.py (Wolf
// 1985, Rosenstein 1993, Sample Entropy, correlation dimension) with the
// later additions: the Rosenstein fit over a fraction of the divergence
// curve's rise, an absolute SampEn tolerance, AMI/FNN for the calibration.
#pragma once

#include <QList>
#include <QPair>
#include <QString>

#include <array>
#include <optional>
#include <vector>

#include "fx/config.h"

namespace skg::fx {

// --- statistics
// ----------------------------------------------------------------
struct Moments {
  double mean, var, m3, m4; // population moments
};
Moments moments(const std::vector<double> &x);
double peak(const std::vector<double> &x);
double rms(const std::vector<double> &x);
double zeroCrossingRate(const std::vector<double> &x);
double dominantFrequency(const std::vector<double> &power,
                         const std::vector<double> &freqs);
// centroid, low-frequency energy fraction, high-frequency energy fraction
std::array<double, 3>
spectralCentroidAndEnergy(const std::vector<double> &freqs,
                          const std::vector<double> &power, double splitHz);
std::array<double, 3>
hjorth(const std::vector<double> &x); // activity, mobility, complexity
double permutationEntropy(
    const std::vector<double> &x); // order 3, delay 1, normalised
double crossCorrelation(const std::vector<double> &a,
                        const std::vector<double> &b);

// --- chaos
// ------------------------------------------------------------------------- All
// take a z-score normalised series unless noted.
double wolfLye(const std::vector<double> &x, double fs, int tau, int dim,
               int evolve);
// Mean log divergence per step, up to jmax, neighbours outside `band`.
std::vector<double> divergenceCurve(const std::vector<double> &x, int tau,
                                    int dim, int band, int jmax);
// Slopes over the curve's rise: (short, long) per mean period.
std::pair<double, double> riseFit(const std::vector<double> &curve, double fs,
                                  double meanPeriod,
                                  std::array<double, 2> rise);
std::pair<double, double> rosensteinLye(const std::vector<double> &x, double fs,
                                        int tau, int dim, double meanPeriod,
                                        double horizon,
                                        std::array<double, 2> rise);
// The original fixed-window fit (slope in mean periods: short lo/hi, long
// lo/hi), kept for comparison with older outputs.
std::pair<double, double> rosensteinFixedWindow(const std::vector<double> &x,
                                                double fs, int tau, int dim,
                                                std::array<double, 4> slope,
                                                double meanPeriod);
double sampleEntropy(const std::vector<double> &x, int m,
                     double r); // r times own std
double sampleEntropyTol(const std::vector<double> &x, int m,
                        double tol); // absolute
double correlationDimension(const std::vector<double> &x, int tau, int de);
// Embedding delay from the first minimum of the average mutual information.
std::optional<int> amiTau(const std::vector<double> &x, int maxLag);
// Embedding dimension by False Nearest Neighbours (Kennel 1992).
std::optional<int> fnnDim(const std::vector<double> &x, int tau, int maxDim);
double pythonRound(double v); // round half to even

// --- a window
// --------------------------------------------------------------------
using FeatureRow = QList<QPair<QString, double>>;

// Feature names in output order (alphabetical, as sismokaos-cli writes them).
QStringList featureNames(bool chaosOnly);
// Every feature of one three-component window. A component with a NaN gets
// NaN for all its features (never a missing one).
FeatureRow computeWindow(const std::vector<double> &e,
                         const std::vector<double> &n,
                         const std::vector<double> &z, const Config &cfg);

} // namespace skg::fx
