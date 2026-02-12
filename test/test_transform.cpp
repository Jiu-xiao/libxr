#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

void test_transform()
{
  LibXR::Position pos(1., 8., 0.3);
  LibXR::Position pos_new;
  LibXR::EulerAngle eulr = {M_PI / 12, M_PI / 6, M_PI / 4}, eulr_new;
  LibXR::RotationMatrix rot, rot_new;
  LibXR::Quaternion quat, quat_new;

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