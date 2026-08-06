# Sensirion Gas Index Algorithm provenance

- Upstream: https://github.com/Sensirion/gas-index-algorithm
- Revision: `2ef9f13d225e8a0dedd3ff42dd229af4dbb1aae4`
- Directory: `sensirion_gas_index_algorithm`
- Algorithm API version macro: `3.2.0`
- License: BSD-3-Clause, reproduced in `LICENSE`
- SiteTwin changes: repository line-ending normalization only; algorithm logic and public declarations are unchanged.

SiteTwin uses the floating-point VOC implementation. The low-level SGP40 I2C protocol and SiteTwin lifecycle wrapper remain separate from this third-party algorithm state.
