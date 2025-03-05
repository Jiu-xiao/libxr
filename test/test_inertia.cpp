#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

void test_inertia() {
  LibXR::Position pos(1., 8., 0.3);
  LibXR::Position pos_new;
  LibXR::EulerAngle eulr = {M_PI / 12, M_PI / 6, M_PI / 4}, eulr_new;
  LibXR::RotationMatrix rot, rot_new;
  LibXR::Quaternion quat, quat_new;

  double i_xx = 1., i_yy = 1., i_zz = 1., i_xy = 0., i_xz = 0., i_yz = 0.;
  pos = LibXR::Position(std::sqrt(0.5), std::sqrt(0.5), 0.);
  eulr = LibXR::EulerAngle(0., 0., M_PI / 4);

  LibXR::Inertia inertia(0.1, i_xx, i_yy, i_zz, i_xy, i_xz, i_yz);

  auto inertia_new = inertia.Translate(pos);
  inertia_new = inertia_new.Rotate(eulr.toQuaternion());

  ASSERT(equal(inertia_new(0, 0), 1.1) && equal(inertia_new(0, 1), 0.) &&
         equal(inertia_new(0, 2), 0.) && equal(inertia_new(1, 0), 0.) &&
         equal(inertia_new(1, 1), 1.) && equal(inertia_new(1, 2), 0.) &&
         equal(inertia_new(2, 0), 0.) && equal(inertia_new(2, 1), 0.) &&
         equal(inertia_new(2, 2), 1.1));

  inertia_new = inertia.Translate(pos);
  inertia_new = inertia_new.Rotate(eulr.toRotationMatrix());

  ASSERT(equal(inertia_new(0, 0), 1.1) && equal(inertia_new(0, 1), 0.) &&
         equal(inertia_new(0, 2), 0.) && equal(inertia_new(1, 0), 0.) &&
         equal(inertia_new(1, 1), 1.) && equal(inertia_new(1, 2), 0.) &&
         equal(inertia_new(2, 0), 0.) && equal(inertia_new(2, 1), 0.) &&
         equal(inertia_new(2, 2), 1.1));
}