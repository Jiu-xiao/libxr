#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

void test_inertia()
{
  using LibXR::CenterOfMass;
  using LibXR::Inertia;
  using LibXR::Position;
  using LibXR::Quaternion;
  using LibXR::Transform;

  /* Constructor checks */
  double data9[9] = {1., 2., 3., 4., 5., 6., 7., 8., 9.};
  double data33[3][3] = {{1., 2., 3.}, {4., 5., 6.}, {7., 8., 9.}};
  double data6[6] = {1., 2., 3., 4., 5., 6.};

  Inertia from_arr9(0.1, data9);
  Inertia from_mat(0.1, data33);
  Inertia from_sym(0.1, data6);
  Inertia from_vals(0.1, 1., 2., 3., 4., 5., 6.);

  for (int i = 0; i < 3; ++i)
  {
    for (int j = 0; j < 3; ++j)
    {
      ASSERT(equal(from_arr9(i, j), from_mat(i, j)));
    }
  }

  ASSERT(equal(from_sym(0, 0), 1.) && equal(from_sym(1, 1), 2.) &&
         equal(from_sym(2, 2), 3.) && equal(from_sym(0, 1), -4.) &&
         equal(from_sym(1, 0), -4.) && equal(from_sym(0, 2), -6.) &&
         equal(from_sym(2, 0), -6.) && equal(from_sym(1, 2), -5.) &&
         equal(from_sym(2, 1), -5.));

  for (int i = 0; i < 9; ++i)
  {
    ASSERT(equal(from_arr9.data[i], from_vals.data[i]));
  }

  /* Translation and rotation checks */
  Position pos(std::sqrt(0.5), std::sqrt(0.5), 0.);
  auto translated = from_vals.Translate(pos);
  ASSERT(equal(translated(0, 0), 1.05) && equal(translated(0, 1), -0.05) &&
         equal(translated(1, 0), -0.05) && equal(translated(1, 1), 2.05) &&
         equal(translated(2, 2), 3.1));

  auto rotated_q = translated.Rotate(Quaternion<>(0.9238795, 0., 0., 0.3826834));
  auto rotated_m = translated.Rotate(
      Quaternion<>(0.9238795, 0., 0., 0.3826834).ToRotationMatrix());

  for (int i = 0; i < 3; ++i)
  {
    for (int j = 0; j < 3; ++j)
    {
      ASSERT(equal(rotated_q(i, j), rotated_m(i, j)));
    }
  }

  /* Matrix addition */
  Eigen::Matrix<double, 3, 3> m_add;
  m_add << 1., 2., 3., 4., 5., 6., 7., 8., 9.;
  auto m_sum = from_vals + m_add;
  ASSERT(equal(m_sum(0, 0), from_vals(0, 0) + 1.) &&
         equal(m_sum(1, 1), from_vals(1, 1) + 5.) &&
         equal(m_sum(2, 2), from_vals(2, 2) + 9.));

  /* Center of mass combination */
  Transform t1(Quaternion<>(), Position(1., 0., 0.));
  Transform t2(Quaternion<>(), Position(0., 1., 0.));
  CenterOfMass<> c1(from_vals, t1);
  CenterOfMass<> c2(from_arr9, t2);
  auto c = c1 + c2;
  ASSERT(equal(c.mass, 0.2));
  ASSERT(equal(c.position(0), 0.5) && equal(c.position(1), 0.5) &&
         equal(c.position(2), 0.));

  /* Original behaviour check */
  auto inertia_new = Inertia(0.1, 1., 1., 1., 0., 0., 0.)
                         .Translate(pos)
                         .Rotate(LibXR::EulerAngle(0., 0., M_PI / 4).ToQuaternion());

  ASSERT(equal(inertia_new(0, 0), 1.1) && equal(inertia_new(0, 1), 0.) &&
         equal(inertia_new(0, 2), 0.) && equal(inertia_new(1, 0), 0.) &&
         equal(inertia_new(1, 1), 1.) && equal(inertia_new(1, 2), 0.) &&
         equal(inertia_new(2, 0), 0.) && equal(inertia_new(2, 1), 0.) &&
         equal(inertia_new(2, 2), 1.1));

  inertia_new = Inertia(0.1, 1., 1., 1., 0., 0., 0.)
                    .Translate(pos)
                    .Rotate(LibXR::EulerAngle(0., 0., M_PI / 4).ToRotationMatrix());

  ASSERT(equal(inertia_new(0, 0), 1.1) && equal(inertia_new(0, 1), 0.) &&
         equal(inertia_new(0, 2), 0.) && equal(inertia_new(1, 0), 0.) &&
         equal(inertia_new(1, 1), 1.) && equal(inertia_new(1, 2), 0.) &&
         equal(inertia_new(2, 0), 0.) && equal(inertia_new(2, 1), 0.) &&
         equal(inertia_new(2, 2), 1.1));
}
