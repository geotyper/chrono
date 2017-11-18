// =============================================================================
// PROJECT CHRONO - http://projectchrono.org
//
// Copyright (c) 2014 projectchrono.org
// All rights reserved.
//
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file at the top level of the distribution and at
// http://projectchrono.org/license-chrono.txt.
//
// =============================================================================
// Authors: Alessandro Tasora, Radu Serban
// =============================================================================

#include "chrono/physics/ChLoadsBody.h"

namespace chrono {

// -----------------------------------------------------------------------------
// ChLoadBodyForce
// -----------------------------------------------------------------------------

ChLoadBodyForce::ChLoadBodyForce(std::shared_ptr<ChBody> mbody,
                                 const ChVector<>& mforce,
                                 bool mlocal_force,
                                 const ChVector<>& mapplication,
                                 bool mlocal_application)
    : ChLoadCustom(mbody),
      force(mforce),
      application(mapplication),
      local_force(mlocal_force),
      local_application(mlocal_application) {}

void ChLoadBodyForce::ComputeQ(ChState* state_x, ChStateDelta* state_w) {
    auto mbody = std::dynamic_pointer_cast<ChBody>(this->loadable);
    double detJ;  // not used

    ChVector<> abs_force;
    if (this->local_force)
        abs_force = mbody->TransformDirectionLocalToParent(force);
    else
        abs_force = force;

    ChVector<> abs_application;
    if (this->local_application)
        abs_application = mbody->TransformPointLocalToParent(application);
    else
        abs_application = application;

    // ChBody assumes F={force_abs, torque_abs}
    ChVectorDynamic<> mF(loadable->Get_field_ncoords());
    mF(0) = abs_force.x();
    mF(1) = abs_force.y();
    mF(2) = abs_force.z();

    // Compute Q = N(u,v,w)'*F
    mbody->ComputeNF(abs_application.x(), abs_application.y(), abs_application.z(), load_Q, detJ, mF, state_x, state_w);
}

// -----------------------------------------------------------------------------
// ChLoadBodyTorque
// -----------------------------------------------------------------------------

ChLoadBodyTorque::ChLoadBodyTorque(std::shared_ptr<ChBody> mbody, const ChVector<>& torque, bool mlocal_torque)
    : ChLoadCustom(mbody), torque(torque), local_torque(mlocal_torque) {}

void ChLoadBodyTorque::ComputeQ(ChState* state_x, ChStateDelta* state_w) {
    auto mbody = std::dynamic_pointer_cast<ChBody>(this->loadable);
    double detJ;  // not used

    ChVector<> abs_torque;
    if (this->local_torque)
        abs_torque = mbody->TransformDirectionLocalToParent(torque);
    else
        abs_torque = torque;

    // ChBody assumes F={force_abs, torque_abs}
    ChVectorDynamic<> mF(loadable->Get_field_ncoords());
    mF(3) = torque.x();
    mF(4) = torque.y();
    mF(5) = torque.z();

    // Compute Q = N(u,v,w)'*F
    mbody->ComputeNF(0, 0, 0, load_Q, detJ, mF, state_x, state_w);
}

// -----------------------------------------------------------------------------
// ChLoadBodyBody
// -----------------------------------------------------------------------------

ChLoadBodyBody::ChLoadBodyBody(std::shared_ptr<ChBody> mbodyA,
                               std::shared_ptr<ChBody> mbodyB,
                               const ChFrame<>& abs_application)
    : ChLoadCustomMultiple(mbodyA, mbodyB) {
    mbodyA->ChFrame::TransformParentToLocal(abs_application, loc_application_A);
    mbodyB->ChFrame::TransformParentToLocal(abs_application, loc_application_B);
}

void ChLoadBodyBody::ComputeQ(ChState* state_x, ChStateDelta* state_w) {
    auto mbodyA = std::dynamic_pointer_cast<ChBody>(this->loadables[0]);
    auto mbodyB = std::dynamic_pointer_cast<ChBody>(this->loadables[1]);

    ChFrameMoving<> bodycoordA, bodycoordB;
    if (state_x) {
        // the numerical jacobian algo might change state_x
        bodycoordA.SetCoord(state_x->ClipCoordsys(0, 0));
        bodycoordB.SetCoord(state_x->ClipCoordsys(7, 0));
    } else {
        bodycoordA.SetCoord(mbodyA->coord);
        bodycoordB.SetCoord(mbodyB->coord);
    }

    if (state_w) {
        // the numerical jacobian algo might change state_w
        bodycoordA.SetPos_dt(state_w->ClipVector(0, 0));
        bodycoordA.SetWvel_loc(state_w->ClipVector(3, 0));
        bodycoordB.SetPos_dt(state_w->ClipVector(6, 0));
        bodycoordB.SetWvel_loc(state_w->ClipVector(9, 0));
    } else {
        bodycoordA.SetCoord_dt(mbodyA->GetCoord_dt());
        bodycoordB.SetCoord_dt(mbodyB->GetCoord_dt());
    }

    frame_Aw = ChFrameMoving<>(loc_application_A) >> bodycoordA;
    frame_Bw = ChFrameMoving<>(loc_application_B) >> bodycoordB;
    ChFrameMoving<> rel_AB = frame_Aw >> frame_Bw.GetInverse();

    // OMPUTE THE FORCE

    this->ComputeBushingForceTorque(rel_AB, locB_force, locB_torque);

    ChVector<> abs_force = frame_Bw.TransformDirectionLocalToParent(locB_force);
    ChVector<> abs_torque = frame_Bw.TransformDirectionLocalToParent(locB_torque);

    // Compute Q

    ChVector<> loc_ftorque = bodycoordA.GetRot().RotateBack(((frame_Aw.GetPos() - bodycoordA.GetPos()) % -abs_force));
    ChVector<> loc_torque = bodycoordA.GetRot().RotateBack(-abs_torque);
    this->load_Q.PasteVector(-abs_force, 0, 0);
    this->load_Q.PasteVector(loc_ftorque + loc_torque, 3, 0);

    loc_ftorque = bodycoordB.GetRot().RotateBack(((frame_Bw.GetPos() - bodycoordB.GetPos()) % abs_force));
    loc_torque = bodycoordB.GetRot().RotateBack(abs_torque);
    this->load_Q.PasteVector(abs_force, 6, 0);
    this->load_Q.PasteVector(loc_ftorque + loc_torque, 9, 0);
}

std::shared_ptr<ChBody> ChLoadBodyBody::GetBodyA() const {
    return std::dynamic_pointer_cast<ChBody>(this->loadables[0]);
}

std::shared_ptr<ChBody> ChLoadBodyBody::GetBodyB() const {
    return std::dynamic_pointer_cast<ChBody>(this->loadables[1]);
}

// -----------------------------------------------------------------------------
// ChLoadBodyBodyBushingSpherical
// -----------------------------------------------------------------------------

ChLoadBodyBodyBushingSpherical::ChLoadBodyBodyBushingSpherical(std::shared_ptr<ChBody> mbodyA,
                                                               std::shared_ptr<ChBody> mbodyB,
                                                               const ChFrame<>& abs_application,
                                                               const ChVector<>& mstiffness,
                                                               const ChVector<>& mdamping)
    : ChLoadBodyBody(mbodyA, mbodyB, abs_application), stiffness(mstiffness), damping(mdamping) {}

void ChLoadBodyBodyBushingSpherical::ComputeBushingForceTorque(const ChFrameMoving<>& rel_AB,
                                                               ChVector<>& loc_force,
                                                               ChVector<>& loc_torque) {
    loc_force = rel_AB.GetPos() * stiffness      // element-wise product!
                + rel_AB.GetPos_dt() * damping;  // element-wise product!
    loc_torque = VNULL;
}

// -----------------------------------------------------------------------------
// ChLoadBodyBodyBushingPlastic
// -----------------------------------------------------------------------------

ChLoadBodyBodyBushingPlastic::ChLoadBodyBodyBushingPlastic(std::shared_ptr<ChBody> mbodyA,
                                                           std::shared_ptr<ChBody> mbodyB,
                                                           const ChFrame<>& abs_application,
                                                           const ChVector<>& mstiffness,
                                                           const ChVector<>& mdamping,
                                                           const ChVector<>& myield)
    : ChLoadBodyBodyBushingSpherical(mbodyA, mbodyB, abs_application, mstiffness, mdamping),
      yield(myield),
      plastic_def(VNULL) {}

void ChLoadBodyBodyBushingPlastic::ComputeBushingForceTorque(const ChFrameMoving<>& rel_AB,
                                                             ChVector<>& loc_force,
                                                             ChVector<>& loc_torque) {
    loc_force = (rel_AB.GetPos() - plastic_def) * stiffness  // element-wise product!
                + rel_AB.GetPos_dt() * damping;              // element-wise product!

    // A basic plasticity, assumed with box capping, without hardening:

    if (loc_force.x() > yield.x()) {
        loc_force.x() = yield.x();
        plastic_def.x() = rel_AB.GetPos().x() - loc_force.x() / stiffness.x();
    }
    if (loc_force.x() < -yield.x()) {
        loc_force.x() = -yield.x();
        plastic_def.x() = rel_AB.GetPos().x() - loc_force.x() / stiffness.x();
    }
    if (loc_force.y() > yield.y()) {
        loc_force.y() = yield.y();
        plastic_def.y() = rel_AB.GetPos().y() - loc_force.y() / stiffness.y();
    }
    if (loc_force.y() < -yield.y()) {
        loc_force.y() = -yield.y();
        plastic_def.y() = rel_AB.GetPos().y() - loc_force.y() / stiffness.y();
    }
    if (loc_force.z() > yield.z()) {
        loc_force.z() = yield.z();
        plastic_def.z() = rel_AB.GetPos().z() - loc_force.z() / stiffness.z();
    }
    if (loc_force.z() < -yield.z()) {
        loc_force.z() = -yield.z();
        plastic_def.z() = rel_AB.GetPos().z() - loc_force.z() / stiffness.z();
    }

    // GetLog() << "loc_force" << loc_force << "\n";
    // GetLog() << "plastic_def" << plastic_def << "\n";
    loc_torque = VNULL;
}

// -----------------------------------------------------------------------------
// ChLoadBodyBodyBushingMate
// -----------------------------------------------------------------------------

ChLoadBodyBodyBushingMate::ChLoadBodyBodyBushingMate(std::shared_ptr<ChBody> mbodyA,
                                                     std::shared_ptr<ChBody> mbodyB,
                                                     const ChFrame<>& abs_application,
                                                     const ChVector<>& mstiffness,
                                                     const ChVector<>& mdamping,
                                                     const ChVector<>& mrotstiffness,
                                                     const ChVector<>& mrotdamping)
    : ChLoadBodyBodyBushingSpherical(mbodyA, mbodyB, abs_application, mstiffness, mdamping),
      rot_stiffness(mrotstiffness),
      rot_damping(mrotdamping) {}

void ChLoadBodyBodyBushingMate::ComputeBushingForceTorque(const ChFrameMoving<>& rel_AB,
                                                          ChVector<>& loc_force,
                                                          ChVector<>& loc_torque) {
    // inherit parent to compute loc_force = ...
    ChLoadBodyBodyBushingSpherical::ComputeBushingForceTorque(rel_AB, loc_force, loc_torque);

    // compute local torque using small rotations:
    ChQuaternion<> rel_rot = rel_AB.GetRot();

    ChVector<> dir_rot;
    double angle_rot;
    rel_rot.Q_to_AngAxis(angle_rot, dir_rot);
    if (angle_rot > CH_C_PI)
        angle_rot -= CH_C_2PI;
    if (angle_rot < -CH_C_PI)
        angle_rot += CH_C_2PI;
    ChVector<> vect_rot = dir_rot * angle_rot;

    loc_torque = vect_rot * rot_stiffness           // element-wise product!
                 + rel_AB.GetWvel_par() * damping;  // element-wise product!
}

// -----------------------------------------------------------------------------
// ChLoadBodyBodyBushingGeneric
// -----------------------------------------------------------------------------

ChLoadBodyBodyBushingGeneric::ChLoadBodyBodyBushingGeneric(std::shared_ptr<ChBody> mbodyA,
                                                           std::shared_ptr<ChBody> mbodyB,
                                                           const ChFrame<>& abs_application,
                                                           const ChMatrix<>& mstiffness,
                                                           const ChMatrix<>& mdamping)
    : ChLoadBodyBody(mbodyA, mbodyB, abs_application), stiffness(mstiffness), damping(mdamping) {}

void ChLoadBodyBodyBushingGeneric::ComputeBushingForceTorque(const ChFrameMoving<>& rel_AB,
                                                             ChVector<>& loc_force,
                                                             ChVector<>& loc_torque) {
    // compute local force & torque (assuming small rotations):
    ChVectorDynamic<> mF(6);
    ChVectorDynamic<> mS(6);
    ChVectorDynamic<> mSdt(6);
    ChVector<> rel_pos = rel_AB.GetPos() + neutral_displacement.GetPos();
    ChQuaternion<> rel_rot = rel_AB.GetRot() * neutral_displacement.GetRot();
    ChVector<> dir_rot;
    double angle_rot;
    rel_rot.Q_to_AngAxis(angle_rot, dir_rot);
    if (angle_rot > CH_C_PI)
        angle_rot -= CH_C_2PI;
    if (angle_rot < -CH_C_PI)
        angle_rot += CH_C_2PI;
    ChVector<> vect_rot = dir_rot * angle_rot;

    mS.PasteVector(rel_pos, 0, 0);
    mS.PasteVector(vect_rot, 3, 0);
    mSdt.PasteVector(rel_AB.GetPos_dt(), 0, 0);
    mSdt.PasteVector(rel_AB.GetWvel_par(), 3, 0);

    mF = stiffness * mS + damping * mSdt;

    loc_force = mF.ClipVector(0, 0) - neutral_force;
    loc_torque = mF.ClipVector(3, 0) - neutral_torque;
}

}  // end namespace chrono
