// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "Evolution/Systems/NewtonianEuler/Fluxes.hpp"

#include <array>

#include "DataStructures/DataVector.hpp"
#include "DataStructures/Tensor/Tensor.hpp"
#include "Utilities/GenerateInstantiations.hpp"
#include "Utilities/Gsl.hpp"

namespace NewtonianEuler {
namespace detail {
template <size_t Dim>
void fluxes_impl(
    const gsl::not_null<tnsr::I<DataVector, Dim>*> mass_density_cons_flux,
    const gsl::not_null<tnsr::IJ<DataVector, Dim>*> momentum_density_flux,
    const gsl::not_null<tnsr::I<DataVector, Dim>*> energy_density_flux,
    const gsl::not_null<Scalar<DataVector>*> enthalpy_density,
    const tnsr::I<DataVector, Dim>& momentum_density,
    const Scalar<DataVector>& energy_density,
    const tnsr::I<DataVector, Dim>& velocity,
    const Scalar<DataVector>& pressure) {
  get(*enthalpy_density) = get(energy_density) + get(pressure);

  for (size_t i = 0; i < Dim; ++i) {
    mass_density_cons_flux->get(i) = momentum_density.get(i);
    for (size_t j = 0; j < Dim; ++j) {
      momentum_density_flux->get(i, j) =
          momentum_density.get(i) * velocity.get(j);
    }
    momentum_density_flux->get(i, i) += get(pressure);
    energy_density_flux->get(i) = get(*enthalpy_density) * velocity.get(i);
  }
}
}  // namespace detail

template <size_t Dim>
void ComputeFluxes<Dim>::apply(
    const gsl::not_null<tnsr::I<DataVector, Dim>*> mass_density_cons_flux,
    const gsl::not_null<tnsr::IJ<DataVector, Dim>*> momentum_density_flux,
    const gsl::not_null<tnsr::I<DataVector, Dim>*> energy_density_flux,
    const tnsr::I<DataVector, Dim>& momentum_density,
    const Scalar<DataVector>& energy_density,
    const tnsr::I<DataVector, Dim>& velocity,
    const Scalar<DataVector>& pressure) {
  Scalar<DataVector> enthalpy_density{get<0>(momentum_density).size()};
  detail::fluxes_impl(mass_density_cons_flux, momentum_density_flux,
                      energy_density_flux, make_not_null(&enthalpy_density),
                      momentum_density, energy_density, velocity, pressure);
}

}  // namespace NewtonianEuler

#define DIM(data) BOOST_PP_TUPLE_ELEM(0, data)

#define INSTANTIATE(_, data)                                                 \
  template void NewtonianEuler::detail::fluxes_impl<DIM(data)>(              \
      gsl::not_null<tnsr::I<DataVector, DIM(data)>*> mass_density_cons_flux, \
      gsl::not_null<tnsr::IJ<DataVector, DIM(data)>*> momentum_density_flux, \
      gsl::not_null<tnsr::I<DataVector, DIM(data)>*> energy_density_flux,    \
      gsl::not_null<Scalar<DataVector>*> enthalpy_density,                   \
      const tnsr::I<DataVector, DIM(data)>& momentum_density,                \
      const Scalar<DataVector>& energy_density,                              \
      const tnsr::I<DataVector, DIM(data)>& velocity,                        \
      const Scalar<DataVector>& pressure);                                   \
  template class NewtonianEuler::ComputeFluxes<DIM(data)>;

GENERATE_INSTANTIATIONS(INSTANTIATE, (1, 2, 3))

#undef DIM
#undef INSTANTIATE
