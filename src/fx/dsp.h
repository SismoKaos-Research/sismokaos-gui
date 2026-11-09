// Signal processing for the pipeline: a zero-phase Butterworth bandpass as
// second-order sections (port of sismokaos-cli's filter.rs, which follows
// scipy's butter(N, [lo, hi], 'band')), a mixed-size FFT, gap interpolation,
// linear detrending and the moving-average STA/LTA.
#pragma once

#include <complex>
#include <vector>

namespace skg::fx {

class BandPass {
public:
  // Order-`order` Butterworth prototype (so a bandpass of order 2*order)
  // passing lo..hi Hz at rate fs. Throws std::invalid_argument.
  BandPass(int order, double fs, double lo, double hi);
  // Zero-phase filtering as scipy's sosfiltfilt: odd reflection of `padlen`
  // samples at both ends, each pass started from steady state.
  std::vector<double> filtfilt(const std::vector<double> &x, int padlen) const;
  double magnitude(double freq, double fs) const;
  const std::vector<std::array<double, 5>> &sections() const {
    return sections_;
  }

private:
  void forward(std::vector<double> &x) const;
  std::vector<std::array<double, 5>> sections_; // b0 b1 b2 a1 a2
};

// Seconds for a bandpass with lower corner `freqmin` to ring down.
double settleSeconds(double freqmin);

// Complex FFT of any length (radix-2 for powers of two, Bluestein otherwise).
void fft(std::vector<std::complex<double>> &a);
// |X_k|^2 / n for k in 0..n/2, the one-sided power spectrum the features use.
std::vector<double> powerSpectrum(const std::vector<double> &x);

// Fills interior NaN runs with a Catmull-Rom Hermite spline between the
// bracketing samples; edge gaps stay NaN. Returns which samples it filled.
std::vector<bool> interpolateGaps(std::vector<double> &data);
// Removes the least-squares line, ignoring NaNs.
void detrendLinear(std::vector<double> &data);

// Classic STA/LTA on squared samples over the part where the LTA is full
// (cft[nlta..]); max, mean and median of it. NaNs when it cannot be computed.
struct StaLta {
  double max, mean, median;
};
StaLta staLta(const std::vector<double> &x, int nsta, int nlta);

} // namespace skg::fx
