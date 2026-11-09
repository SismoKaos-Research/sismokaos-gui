# XGBoost (git submodule at `third_party/xgboost`)

- Source: https://github.com/dmlc/xgboost, tag `v3.4.2`
  (commit fdf0888bedddbd444d72994d845c59b3ca182c5b), with its `dmlc-core`
  submodule, added 2026-11-09. Nothing in the submodule is modified; build
  options are set in `third_party/CMakeLists.txt` (static library, no CUDA,
  OpenMP on Linux and Windows only).
- Licence: Apache 2.0 (see `third_party/xgboost/LICENSE`); dmlc-core Apache 2.0.
- Used for: the tree models of the classification port (xgboost itself,
  gradient boosting, random forest through the parallel-tree mode) and the
  logistic regression (the `gblinear` booster). Models are saved in
  XGBoost's JSON format, which the Python package also reads.
- Fetch after cloning: `git submodule update --init --recursive`.
