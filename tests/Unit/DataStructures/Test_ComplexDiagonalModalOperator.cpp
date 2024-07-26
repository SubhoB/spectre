// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "Framework/TestingFramework.hpp"

#include <complex>
#include <tuple>

#include "DataStructures/ComplexDiagonalModalOperator.hpp"
#include "DataStructures/DiagonalModalOperator.hpp"
#include "DataStructures/ModalVector.hpp"
#include "Helpers/DataStructures/VectorImplTestHelper.hpp"
#include "Utilities/ErrorHandling/Error.hpp"
#include "Utilities/Functional.hpp"
#include "Utilities/StdHelpers.hpp"
#include "Utilities/TypeTraits.hpp"

namespace {
void test_complex_diagonal_modal_operator_math() {
  const TestHelpers::VectorImpl::Bound generic{{-100.0, 100.0}};
  const TestHelpers::VectorImpl::Bound positive{{0.01, 100.0}};

  const auto unary_ops = std::make_tuple(
      std::make_tuple(funcl::Conj<>{}, std::make_tuple(generic)),
      std::make_tuple(funcl::Imag<>{}, std::make_tuple(generic)),
      std::make_tuple(funcl::Real<>{}, std::make_tuple(generic)),
      std::make_tuple(funcl::Sqrt<>{}, std::make_tuple(generic)));

  TestHelpers::VectorImpl::test_functions_with_vector_arguments<
      TestHelpers::VectorImpl::TestKind::Normal, ComplexDiagonalModalOperator>(
      unary_ops);

  const auto binary_ops = std::make_tuple(
      std::make_tuple(funcl::Divides<>{}, std::make_tuple(generic, positive)),
      std::make_tuple(funcl::Minus<>{}, std::make_tuple(generic, generic)),
      std::make_tuple(funcl::Multiplies<>{}, std::make_tuple(generic, generic)),
      std::make_tuple(funcl::Plus<>{}, std::make_tuple(generic, generic)));

  TestHelpers::VectorImpl::test_functions_with_vector_arguments<
      TestHelpers::VectorImpl::TestKind::Normal, ComplexDiagonalModalOperator>(
      binary_ops);

  TestHelpers::VectorImpl::test_functions_with_vector_arguments<
      TestHelpers::VectorImpl::TestKind::Normal, ComplexDiagonalModalOperator,
      DiagonalModalOperator>(binary_ops);

  const auto inplace_binary_ops = std::make_tuple(
      std::make_tuple(funcl::DivAssign<>{}, std::make_tuple(generic, positive)),
      std::make_tuple(funcl::MinusAssign<>{},
                      std::make_tuple(generic, generic)),
      std::make_tuple(funcl::MultAssign<>{}, std::make_tuple(generic, generic)),
      std::make_tuple(funcl::PlusAssign<>{},
                      std::make_tuple(generic, generic)));

  TestHelpers::VectorImpl::test_functions_with_vector_arguments<
      TestHelpers::VectorImpl::TestKind::Inplace, ComplexDiagonalModalOperator,
      DiagonalModalOperator>(inplace_binary_ops);

  // Note that a collection of additional operations that involve acting on
  // complex modal vectors with complex diagonal modal operators have been moved
  // to `Test_MoreComplexDiagonalModalOperatorMath.cpp` in an effort to better
  // parallelize the build.
}
}  // namespace

SPECTRE_TEST_CASE("Unit.DataStructures.ComplexDiagonalModalOperator",
                  "[DataStructures][Unit]") {
  {
    INFO("test construct and assign");
    TestHelpers::VectorImpl::vector_test_construct_and_assign<
        ComplexDiagonalModalOperator, std::complex<double>>();
  }
  {
    INFO("test serialize and deserialize");
    TestHelpers::VectorImpl::vector_test_serialize<ComplexDiagonalModalOperator,
                                                   std::complex<double>>();
  }
  {
    INFO("test set_data_ref functionality");
    TestHelpers::VectorImpl::vector_test_ref<ComplexDiagonalModalOperator,
                                             std::complex<double>>();
  }
  {
    INFO("test math after move");
    TestHelpers::VectorImpl::vector_test_math_after_move<
        ComplexDiagonalModalOperator, std::complex<double>>();
  }
  {
    INFO("test ComplexDiagonalModalOperator math operations");
    test_complex_diagonal_modal_operator_math();
  }

#ifdef SPECTRE_DEBUG
  CHECK_THROWS_WITH(
      TestHelpers::VectorImpl::vector_ref_test_size_error<
          ComplexDiagonalModalOperator>(
          TestHelpers::VectorImpl::RefSizeErrorTestKind::ExpressionAssign),
      Catch::Matchers::ContainsSubstring("Must assign into same size"));
  CHECK_THROWS_WITH(
      TestHelpers::VectorImpl::vector_ref_test_size_error<
          ComplexDiagonalModalOperator>(
          TestHelpers::VectorImpl::RefSizeErrorTestKind::Copy),
      Catch::Matchers::ContainsSubstring("Must copy into same size"));
  CHECK_THROWS_WITH(
      TestHelpers::VectorImpl::vector_ref_test_size_error<
          ComplexDiagonalModalOperator>(
          TestHelpers::VectorImpl::RefSizeErrorTestKind::Move),
      Catch::Matchers::ContainsSubstring("Must copy into same size"));
#endif
}
