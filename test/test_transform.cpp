#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

void test_transform()
{
  LibXR::Position pos(1., 8., 0.3);
  LibXR::Position pos_new;
  LibXR::EulerAngle eulr = {LibXR::PI / 12, LibXR::PI / 6, LibXR::PI / 4}, eulr_new;
  LibXR::RotationMatrix rot, rot_new;
  LibXR::Quaternion quat, quat_new;
  double quat_wxyz[4] = {0.1, 0.2, 0.3, 0.4};
  LibXR::Quaternion quat_from_array(quat_wxyz);
  double rot_row_major[9] = {1.0, 0.0, 0.0, 0.0, 0.0, -1.0, 0.0, 1.0, 0.0};
  double rot_row_major_2d[3][3] = {{1.0, 0.0, 0.0}, {0.0, 0.0, -1.0}, {0.0, 1.0, 0.0}};
  ASSERT(equal(quat_from_array.w(), quat_wxyz[0]) &&
         equal(quat_from_array.x(), quat_wxyz[1]) &&
         equal(quat_from_array.y(), quat_wxyz[2]) &&
         equal(quat_from_array.z(), quat_wxyz[3]));
  ASSERT(equal(quat_from_array(0), quat_wxyz[1]) &&
         equal(quat_from_array(1), quat_wxyz[2]) &&
         equal(quat_from_array(2), quat_wxyz[3]) &&
         equal(quat_from_array(3), quat_wxyz[0]));

  LibXR::RotationMatrix rot_from_array(rot_row_major);
  LibXR::RotationMatrix rot_from_2d_array(rot_row_major_2d);
  ASSERT(equal(rot_from_array(0, 0), 1.0) && equal(rot_from_array(0, 1), 0.0) &&
         equal(rot_from_array(0, 2), 0.0) && equal(rot_from_array(1, 0), 0.0) &&
         equal(rot_from_array(1, 1), 0.0) && equal(rot_from_array(1, 2), -1.0) &&
         equal(rot_from_array(2, 0), 0.0) && equal(rot_from_array(2, 1), 1.0) &&
         equal(rot_from_array(2, 2), 0.0));
  ASSERT(equal(rot_from_2d_array(0, 0), rot_from_array(0, 0)) &&
         equal(rot_from_2d_array(0, 1), rot_from_array(0, 1)) &&
         equal(rot_from_2d_array(0, 2), rot_from_array(0, 2)) &&
         equal(rot_from_2d_array(1, 0), rot_from_array(1, 0)) &&
         equal(rot_from_2d_array(1, 1), rot_from_array(1, 1)) &&
         equal(rot_from_2d_array(1, 2), rot_from_array(1, 2)) &&
         equal(rot_from_2d_array(2, 0), rot_from_array(2, 0)) &&
         equal(rot_from_2d_array(2, 1), rot_from_array(2, 1)) &&
         equal(rot_from_2d_array(2, 2), rot_from_array(2, 2)));

  /* Position */
  rot = eulr.ToRotationMatrix();
  quat = LibXR::Quaternion(rot);

  pos_new = pos * quat;
  quat_new = pos_new / pos;
  rot_new = quat_new.ToRotationMatrix();
  pos_new = pos_new / rot_new;
  ASSERT(equal(pos_new(0), pos(0)) && equal(pos_new(1), pos(1)) &&
         equal(pos_new(2), pos(2)));

  pos_new /= quat;
  pos_new *= rot;
  ASSERT(equal(pos_new(0), pos(0)) && equal(pos_new(1), pos(1)) &&
         equal(pos_new(2), pos(2)));

  pos_new = (pos - pos_new) * 2.;
  pos_new *= 2;
  pos_new /= 4;
  ASSERT(equal(pos_new(0), 0.) && equal(pos_new(1), 0.) && equal(pos_new(2), 0.));

  pos_new = pos + pos_new;
  ASSERT(equal(pos_new(0), pos(0)) && equal(pos_new(1), pos(1)) &&
         equal(pos_new(2), pos(2)));

  pos_new -= pos;

  ASSERT(equal(pos_new(0), 0.) && equal(pos_new(1), 0.) && equal(pos_new(2), 0.));

  pos_new += pos;
  ASSERT(equal(pos_new(0), pos(0)) && equal(pos_new(1), pos(1)) &&
         equal(pos_new(2), pos(2)));

  /* Rotation */
  quat_new = quat;
  quat_new = quat - quat_new;
  quat_new = quat + quat_new;
  ASSERT(equal(quat_new(0), quat(0)) && equal(quat_new(1), quat(1)) &&
         equal(quat_new(2), quat(2)) && equal(quat_new(3), quat(3)));

  Eigen::Quaternion<double> eigen_quat =
      LibXR::EulerAngle<double>(LibXR::PI / 8, -LibXR::PI / 9, LibXR::PI / 7)
          .ToQuaternion();
  LibXR::RotationMatrix rot_from_eigen_quat(eigen_quat);
  rot_new = eigen_quat;
  const Eigen::Matrix3d eigen_rot = eigen_quat.toRotationMatrix();
  ASSERT(equal(rot_from_eigen_quat(0, 0), eigen_rot(0, 0)) &&
         equal(rot_from_eigen_quat(0, 1), eigen_rot(0, 1)) &&
         equal(rot_from_eigen_quat(0, 2), eigen_rot(0, 2)) &&
         equal(rot_from_eigen_quat(1, 0), eigen_rot(1, 0)) &&
         equal(rot_from_eigen_quat(1, 1), eigen_rot(1, 1)) &&
         equal(rot_from_eigen_quat(1, 2), eigen_rot(1, 2)) &&
         equal(rot_from_eigen_quat(2, 0), eigen_rot(2, 0)) &&
         equal(rot_from_eigen_quat(2, 1), eigen_rot(2, 1)) &&
         equal(rot_from_eigen_quat(2, 2), eigen_rot(2, 2)));
  ASSERT(equal(rot_new(0, 0), eigen_rot(0, 0)) && equal(rot_new(0, 1), eigen_rot(0, 1)) &&
         equal(rot_new(0, 2), eigen_rot(0, 2)) && equal(rot_new(1, 0), eigen_rot(1, 0)) &&
         equal(rot_new(1, 1), eigen_rot(1, 1)) && equal(rot_new(1, 2), eigen_rot(1, 2)) &&
         equal(rot_new(2, 0), eigen_rot(2, 0)) && equal(rot_new(2, 1), eigen_rot(2, 1)) &&
         equal(rot_new(2, 2), eigen_rot(2, 2)));
  quat_new = quat / eigen_quat;
  Eigen::Quaternion<double> eigen_div =
      Eigen::Quaternion<double>(quat) * eigen_quat.conjugate();
  ASSERT(equal(quat_new.w(), eigen_div.w()) && equal(quat_new.x(), eigen_div.x()) &&
         equal(quat_new.y(), eigen_div.y()) && equal(quat_new.z(), eigen_div.z()));

  /* ZYX Order */
  rot = eulr.ToRotationMatrixZYX();
  ASSERT(equal(rot(0, 0), 0.6123725) && equal(rot(0, 1), -0.5915064) &&
         equal(rot(0, 2), 0.5245190) && equal(rot(1, 0), 0.6123725) &&
         equal(rot(1, 1), 0.7745190) && equal(rot(1, 2), 0.1584937) &&
         equal(rot(2, 0), -0.5000000) && equal(rot(2, 1), 0.2241439) &&
         equal(rot(2, 2), 0.8365163));

  eulr_new = rot.ToEulerAngleZYX();
  ASSERT(equal(eulr_new(0), eulr(0)) && equal(eulr_new(1), eulr(1)) &&
         equal(eulr_new(2), eulr(2)));

  quat = LibXR::Quaternion(rot);
  quat_new = eulr.ToQuaternionZYX();
  ASSERT(equal(quat_new.w(), quat.w()) && equal(quat_new.x(), quat.x()) &&
         equal(quat_new.y(), quat.y()) && equal(quat_new.z(), quat.z()));

  rot_new = quat.ToRotationMatrix();
  ASSERT(equal(rot_new(0, 0), rot(0, 0)) && equal(rot_new(0, 1), rot(0, 1)) &&
         equal(rot_new(0, 2), rot(0, 2)) && equal(rot_new(1, 0), rot(1, 0)) &&
         equal(rot_new(1, 1), rot(1, 1)) && equal(rot_new(1, 2), rot(1, 2)) &&
         equal(rot_new(2, 0), rot(2, 0)) && equal(rot_new(2, 1), rot(2, 1)) &&
         equal(rot_new(2, 2), rot(2, 2)));

  eulr_new = quat.ToEulerAngleZYX();
  ASSERT(equal(eulr_new(0), eulr(0)) && equal(eulr_new(1), eulr(1)) &&
         equal(eulr_new(2), eulr(2)));

  /* ZXY Order */
  rot = eulr.ToRotationMatrixZXY();
  ASSERT(equal(rot(0, 0), 0.5208661) && equal(rot(0, 1), -0.6830127) &&
         equal(rot(0, 2), 0.5120471) && equal(rot(1, 0), 0.7038788) &&
         equal(rot(1, 1), 0.6830127) && equal(rot(1, 2), 0.1950597) &&
         equal(rot(2, 0), -0.4829629) && equal(rot(2, 1), 0.2588190) &&
         equal(rot(2, 2), 0.8365163));

  eulr_new = rot.ToEulerAngleZXY();
  ASSERT(equal(eulr_new(0), eulr(0)) && equal(eulr_new(1), eulr(1)) &&
         equal(eulr_new(2), eulr(2)));

  quat = LibXR::Quaternion(rot);
  quat_new = eulr.ToQuaternionZXY();
  ASSERT(equal(quat_new.w(), quat.w()) && equal(quat_new.x(), quat.x()) &&
         equal(quat_new.y(), quat.y()) && equal(quat_new.z(), quat.z()));

  rot_new = quat.ToRotationMatrix();
  ASSERT(equal(rot_new(0, 0), rot(0, 0)) && equal(rot_new(0, 1), rot(0, 1)) &&
         equal(rot_new(0, 2), rot(0, 2)) && equal(rot_new(1, 0), rot(1, 0)) &&
         equal(rot_new(1, 1), rot(1, 1)) && equal(rot_new(1, 2), rot(1, 2)) &&
         equal(rot_new(2, 0), rot(2, 0)) && equal(rot_new(2, 1), rot(2, 1)) &&
         equal(rot_new(2, 2), rot(2, 2)));

  eulr_new = quat.ToEulerAngleZXY();
  ASSERT(equal(eulr_new(0), eulr(0)) && equal(eulr_new(1), eulr(1)) &&
         equal(eulr_new(2), eulr(2)));

  /* YXZ Order */
  rot = eulr.ToRotationMatrixYXZ();
  ASSERT(equal(rot(0, 0), 0.7038788) && equal(rot(0, 1), -0.5208661) &&
         equal(rot(0, 2), 0.4829629) && equal(rot(1, 0), 0.6830127) &&
         equal(rot(1, 1), 0.6830127) && equal(rot(1, 2), -0.2588190) &&
         equal(rot(2, 0), -0.1950597) && equal(rot(2, 1), 0.5120471) &&
         equal(rot(2, 2), 0.8365163));

  eulr_new = rot.ToEulerAngleYXZ();
  ASSERT(equal(eulr_new(0), eulr(0)) && equal(eulr_new(1), eulr(1)) &&
         equal(eulr_new(2), eulr(2)));

  quat = LibXR::Quaternion(rot);
  quat_new = eulr.ToQuaternionYXZ();
  ASSERT(equal(quat_new.w(), quat.w()) && equal(quat_new.x(), quat.x()) &&
         equal(quat_new.y(), quat.y()) && equal(quat_new.z(), quat.z()));

  rot_new = quat.ToRotationMatrix();
  ASSERT(equal(rot_new(0, 0), rot(0, 0)) && equal(rot_new(0, 1), rot(0, 1)) &&
         equal(rot_new(0, 2), rot(0, 2)) && equal(rot_new(1, 0), rot(1, 0)) &&
         equal(rot_new(1, 1), rot(1, 1)) && equal(rot_new(1, 2), rot(1, 2)) &&
         equal(rot_new(2, 0), rot(2, 0)) && equal(rot_new(2, 1), rot(2, 1)) &&
         equal(rot_new(2, 2), rot(2, 2)));

  eulr_new = quat.ToEulerAngleYXZ();
  ASSERT(equal(eulr_new(0), eulr(0)) && equal(eulr_new(1), eulr(1)) &&
         equal(eulr_new(2), eulr(2)));

  /* XYZ Order */
  rot = eulr.ToRotationMatrixXYZ();
  ASSERT(equal(rot(0, 0), 0.6123725) && equal(rot(0, 1), -0.6123725) &&
         equal(rot(0, 2), 0.5000000) && equal(rot(1, 0), 0.7745190) &&
         equal(rot(1, 1), 0.5915064) && equal(rot(1, 2), -0.2241439) &&
         equal(rot(2, 0), -0.1584937) && equal(rot(2, 1), 0.5245190) &&
         equal(rot(2, 2), 0.8365163));

  eulr_new = rot.ToEulerAngleXYZ();
  ASSERT(equal(eulr_new(0), eulr(0)) && equal(eulr_new(1), eulr(1)) &&
         equal(eulr_new(2), eulr(2)));

  quat = LibXR::Quaternion(rot);
  quat_new = eulr.ToQuaternionXYZ();
  ASSERT(equal(quat_new.w(), quat.w()) && equal(quat_new.x(), quat.x()) &&
         equal(quat_new.y(), quat.y()) && equal(quat_new.z(), quat.z()));

  rot_new = quat.ToRotationMatrix();
  ASSERT(equal(rot_new(0, 0), rot(0, 0)) && equal(rot_new(0, 1), rot(0, 1)) &&
         equal(rot_new(0, 2), rot(0, 2)) && equal(rot_new(1, 0), rot(1, 0)) &&
         equal(rot_new(1, 1), rot(1, 1)) && equal(rot_new(1, 2), rot(1, 2)) &&
         equal(rot_new(2, 0), rot(2, 0)) && equal(rot_new(2, 1), rot(2, 1)) &&
         equal(rot_new(2, 2), rot(2, 2)));

  eulr_new = quat.ToEulerAngleXYZ();
  ASSERT(equal(eulr_new(0), eulr(0)) && equal(eulr_new(1), eulr(1)) &&
         equal(eulr_new(2), eulr(2)));

  /* XZY Order */
  rot = eulr.ToRotationMatrixXZY();
  ASSERT(equal(rot(0, 0), 0.6123725) && equal(rot(0, 1), -0.7071068) &&
         equal(rot(0, 2), 0.3535534) && equal(rot(1, 0), 0.7209159) &&
         equal(rot(1, 1), 0.6830127) && equal(rot(1, 2), 0.1173625) &&
         equal(rot(2, 0), -0.3244693) && equal(rot(2, 1), 0.1830127) &&
         equal(rot(2, 2), 0.9280227));

  eulr_new = rot.ToEulerAngleXZY();
  ASSERT(equal(eulr_new(0), eulr(0)) && equal(eulr_new(1), eulr(1)) &&
         equal(eulr_new(2), eulr(2)));

  quat = LibXR::Quaternion(rot);
  quat_new = eulr.ToQuaternionXZY();
  ASSERT(equal(quat_new.w(), quat.w()) && equal(quat_new.x(), quat.x()) &&
         equal(quat_new.y(), quat.y()) && equal(quat_new.z(), quat.z()));

  rot_new = quat.ToRotationMatrix();
  ASSERT(equal(rot_new(0, 0), rot(0, 0)) && equal(rot_new(0, 1), rot(0, 1)) &&
         equal(rot_new(0, 2), rot(0, 2)) && equal(rot_new(1, 0), rot(1, 0)) &&
         equal(rot_new(1, 1), rot(1, 1)) && equal(rot_new(1, 2), rot(1, 2)) &&
         equal(rot_new(2, 0), rot(2, 0)) && equal(rot_new(2, 1), rot(2, 1)) &&
         equal(rot_new(2, 2), rot(2, 2)));

  eulr_new = quat.ToEulerAngleXZY();
  ASSERT(equal(eulr_new(0), eulr(0)) && equal(eulr_new(1), eulr(1)) &&
         equal(eulr_new(2), eulr(2)));

  /* YZX Order */
  rot = eulr.ToRotationMatrixYZX();
  ASSERT(equal(rot(0, 0), 0.6123725) && equal(rot(0, 1), -0.4620968) &&
         equal(rot(0, 2), 0.6414565) && equal(rot(1, 0), 0.7071068) &&
         equal(rot(1, 1), 0.6830127) && equal(rot(1, 2), -0.1830127) &&
         equal(rot(2, 0), -0.3535534) && equal(rot(2, 1), 0.5656502) &&
         equal(rot(2, 2), 0.7450100));

  eulr_new = rot.ToEulerAngleYZX();
  ASSERT(equal(eulr_new(0), eulr(0)) && equal(eulr_new(1), eulr(1)) &&
         equal(eulr_new(2), eulr(2)));

  quat = LibXR::Quaternion(rot);
  quat_new = eulr.ToQuaternionYZX();
  ASSERT(equal(quat_new.w(), quat.w()) && equal(quat_new.x(), quat.x()) &&
         equal(quat_new.y(), quat.y()) && equal(quat_new.z(), quat.z()));

  rot_new = quat.ToRotationMatrix();
  ASSERT(equal(rot_new(0, 0), rot(0, 0)) && equal(rot_new(0, 1), rot(0, 1)) &&
         equal(rot_new(0, 2), rot(0, 2)) && equal(rot_new(1, 0), rot(1, 0)) &&
         equal(rot_new(1, 1), rot(1, 1)) && equal(rot_new(1, 2), rot(1, 2)) &&
         equal(rot_new(2, 0), rot(2, 0)) && equal(rot_new(2, 1), rot(2, 1)) &&
         equal(rot_new(2, 2), rot(2, 2)));

  eulr_new = quat.ToEulerAngleYZX();
  ASSERT(equal(eulr_new(0), eulr(0)) && equal(eulr_new(1), eulr(1)) &&
         equal(eulr_new(2), eulr(2)));
}
