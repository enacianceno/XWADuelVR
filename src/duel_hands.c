/* XWADuelVR M3.0 hand tracking implementation. See duel_hands.h. */
#include "duel_hands.h"

#include <stdlib.h>
#include <string.h>

const DuelVec3 DUEL_LEVER_SHIP = { 0.0f, 0.47f, -1.21f };
const DuelVec3 DUEL_LEVER_PIVOT_SHIP = { 0.0f, 0.05f, -1.27f };
const DuelVec3 DUEL_LEVER_PIVOT_MODEL = { 0.0f, 0.05f, 1.27f };
const DuelVec3 DUEL_QUAD_SHIP = { -0.38f, 0.46f, -0.91f };
const DuelVec3 DUEL_PAUSEBTN_SHIP = { 0.3590f, 0.4713f, -1.1595f };
const DuelVec3 DUEL_DASHHP_SHIP = { 0.0f, 0.26f, -0.95f };

/* Bone segments over XR_HAND_JOINT_*_EXT indices (PALM=0 WRIST=1
 * THUMB=2..5 INDEX=6..10 MIDDLE=11..15 RING=16..20 LITTLE=21..25). */
static const int s_bones[DUEL_HAND_BONES][2] = {
    { 1, 0 }, /* wrist -> palm */
    { 0, 2 }, { 0, 6 }, { 0, 11 }, { 0, 16 }, { 0, 21 }, /* palm -> metacarpals */
    { 2, 3 }, { 3, 4 }, { 4, 5 }, /* thumb */
    { 6, 7 }, { 7, 8 }, { 8, 9 }, { 9, 10 }, /* index */
    { 11, 12 }, { 12, 13 }, { 13, 14 }, { 14, 15 }, /* middle */
    { 16, 17 }, { 17, 18 }, { 18, 19 }, { 19, 20 }, /* ring */
    { 21, 22 }, { 22, 23 }, { 23, 24 }, { 24, 25 }, /* little */
};

static PFN_xrCreateHandTrackerEXT s_create = NULL;
static PFN_xrDestroyHandTrackerEXT s_destroy = NULL;
static PFN_xrLocateHandJointsEXT s_locate = NULL;
static PFN_xrGetHandMeshFB s_getmesh = NULL; /* M3.5: optional (NULL ok) */
static XrHandTrackerEXT s_tracker[2] = { XR_NULL_HANDLE, XR_NULL_HANDLE };
static DuelHand s_hands[2];
static XrHandJointLocationEXT s_locs[2][DUEL_HAND_JOINTS];
static XrHandTrackingScaleFB s_scale[2]; /* M3.5: chained, else ignored */
static DuelHandMesh s_mesh[2];
static int s_inited = 0;

static void mesh_free(DuelHandMesh *m);

void DuelHands_Bone(int i, int *a, int *b) {
    if (i < 0) i = 0;
    if (i >= DUEL_HAND_BONES) i = DUEL_HAND_BONES - 1;
    *a = s_bones[i][0];
    *b = s_bones[i][1];
}

int DuelHands_PosValid(XrSpaceLocationFlags flags) {
    return (flags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0;
}

int DuelHands_RotValid(XrSpaceLocationFlags flags) {
    return (flags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0;
}

void DuelHands_JointWorld(const DuelSeatCal *cal,
                           DuelQuat ship_q, DuelVec3 ship_t, DuelVec3 seat_t,
                           DuelVec3 mo_p, DuelQuat mo_q,
                           const DuelHandJoint *j, DuelVec3 *out_p) {
    DuelVec3 cp;
    DuelQuat cq;
    duel_cal_neutralize(cal, j->pos, j->quat, &cp, &cq);
    {
        DuelQuat eq;
        DuelVec3 ep;
        duel_eye_pose_motion(ship_q, ship_t, seat_t, mo_p, mo_q, cq, cp, &eq, &ep);
        *out_p = ep;
    }
}

int DuelHands_Init(PFN_xrGetInstanceProcAddr getProc, XrInstance instance,
                   XrSession session) {
    int mask = 0;
    static const XrHandEXT xr_side[2] = { XR_HAND_LEFT_EXT, XR_HAND_RIGHT_EXT };
    int i;
    if (s_inited) {
        if (s_tracker[0]) mask |= 1;
        if (s_tracker[1]) mask |= 2;
        return mask;
    }
    memset(s_hands, 0, sizeof s_hands);
    memset(s_locs, 0, sizeof s_locs);
    /* The extension must be enabled at instance creation (duel_xr passes it
     * through SDL's XR extension props); without it the lookups fail and
     * hands stay unavailable -- never fatal. */
    if (!getProc || !instance || !session) return 0;
    if (getProc(instance, "xrCreateHandTrackerEXT",
                (PFN_xrVoidFunction *)&s_create) != XR_SUCCESS || !s_create)
        return 0;
    if (getProc(instance, "xrDestroyHandTrackerEXT",
                (PFN_xrVoidFunction *)&s_destroy) != XR_SUCCESS || !s_destroy) {
        s_create = NULL;
        return 0;
    }
    if (getProc(instance, "xrLocateHandJointsEXT",
                (PFN_xrVoidFunction *)&s_locate) != XR_SUCCESS || !s_locate) {
        s_create = NULL;
        s_destroy = NULL;
        return 0;
    }
    /* M3.5: platform mesh is optional -- joints work without it, so a
     * missing lookup only disables the mesh path (diagnostic joints stay). */
    s_getmesh = NULL;
    if (getProc(instance, "xrGetHandMeshFB",
                (PFN_xrVoidFunction *)&s_getmesh) != XR_SUCCESS)
        s_getmesh = NULL;
    memset(s_scale, 0, sizeof s_scale);
    memset(s_mesh, 0, sizeof s_mesh);
    s_mesh[0].scale = 1.0f;
    s_mesh[1].scale = 1.0f;
    for (i = 0; i < 2; i++) {
        XrHandTrackerCreateInfoEXT ci;
        memset(&ci, 0, sizeof ci);
        ci.type = XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT;
        ci.hand = xr_side[i];
        ci.handJointSet = XR_HAND_JOINT_SET_DEFAULT_EXT;
        if (s_create(session, &ci, &s_tracker[i]) == XR_SUCCESS && s_tracker[i]) {
            s_hands[i].tracked = 1;
            mask |= (i == 0) ? 1 : 2;
        } else {
            s_tracker[i] = XR_NULL_HANDLE;
        }
    }
    s_inited = 1;
    return mask;
}

void DuelHands_Shutdown(void) {
    int i;
    if (!s_inited) return;
    for (i = 0; i < 2; i++) {
        mesh_free(&s_mesh[i]);
        if (s_tracker[i] && s_destroy) s_destroy(s_tracker[i]);
        s_tracker[i] = XR_NULL_HANDLE;
    }
    s_create = NULL;
    s_destroy = NULL;
    s_locate = NULL;
    s_getmesh = NULL;
    memset(s_hands, 0, sizeof s_hands);
    s_inited = 0;
}

void DuelHands_Locate(XrSession session, XrSpace baseSpace, XrTime time) {
    int i;
    if (!s_inited || !s_locate || !session || !baseSpace || time <= 0) {
        /* Keep last state on transient failure; mark inactive only when
         * no tracker exists at all. */
        return;
    }
    for (i = 0; i < 2; i++) {
        XrHandJointsLocateInfoEXT li;
        XrHandJointLocationsEXT locs;
        int n;
        if (!s_tracker[i]) {
            s_hands[i].active = 0;
            s_hands[i].n_valid = 0;
            continue;
        }
        memset(&li, 0, sizeof li);
        li.type = XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT;
        li.baseSpace = baseSpace;
        li.time = time;
        memset(&locs, 0, sizeof locs);
        locs.type = XR_TYPE_HAND_JOINT_LOCATIONS_EXT;
        locs.jointCount = DUEL_HAND_JOINTS;
        locs.jointLocations = s_locs[i];
        /* M3.5: request hand scale for bind skinning. Runtimes that do
         * not know the FB struct ignore the chain (output stays 0). */
        memset(&s_scale[i], 0, sizeof s_scale[i]);
        s_scale[i].type = XR_TYPE_HAND_TRACKING_SCALE_FB;
        locs.next = &s_scale[i];
        s_hands[i].locate_calls++;
        s_hands[i].last_result = (int)s_locate(s_tracker[i], &li, &locs);
        if (s_hands[i].last_result != XR_SUCCESS) {
            s_hands[i].active = 0;
            s_hands[i].n_valid = 0;
            continue;
        }
        s_hands[i].active = locs.isActive != 0;
        n = 0;
        if (s_hands[i].active) {
            int k;
            for (k = 0; k < DUEL_HAND_JOINTS; k++) {
                const XrHandJointLocationEXT *l = &s_locs[i][k];
                DuelHandJoint *j = &s_hands[i].j[k];
                j->pos = duel_v3(l->pose.position.x, l->pose.position.y,
                                 l->pose.position.z);
                j->quat = duel_q(l->pose.orientation.x, l->pose.orientation.y,
                                 l->pose.orientation.z, l->pose.orientation.w);
                j->pos_valid = DuelHands_PosValid(l->locationFlags);
                j->rot_valid = DuelHands_RotValid(l->locationFlags);
                n += j->pos_valid ? 1 : 0;
            }
        }
        s_hands[i].n_valid = s_hands[i].active ? n : 0;
        /* M3.5: adopt the live scale for bind skinning (0/unknown -> 1). */
        if (s_scale[i].currentOutput > 1e-6f)
            s_mesh[i].scale = s_scale[i].currentOutput;
        else if (s_mesh[i].scale <= 1e-6f)
            s_mesh[i].scale = 1.0f;
    }
}

const DuelHand *DuelHands_Get(int side) {
    if (side < 0 || side > 1) side = 0;
    return &s_hands[side];
}

static void mesh_free(DuelHandMesh *m) {
    if (!m) return;
    free(m->pos);
    free(m->bi);
    free(m->bw);
    free(m->bpos);
    free(m->bquat);
    free(m->idx);
    memset(m, 0, sizeof *m);
    m->scale = 1.0f;
}

void DuelHands_FreeMesh(int side) {
    if (side < 0 || side > 1) return;
    mesh_free(&s_mesh[side]);
}

const DuelHandMesh *DuelHands_Mesh(int side) {
    if (side < 0 || side > 1) side = 0;
    return &s_mesh[side];
}

/* M3.5: fetch + keep the platform bind mesh (positions, blend, bind
 * joints, indices). Normals/UVs/radii/parents are dropped. Any anomaly
 * (counts, wrong joint count, alloc) -> 0, joints path stays. */
int DuelHands_FetchMesh(int side) {
    XrHandTrackingMeshFB m;
    XrVector3f *vpos = NULL;
    XrVector3f *vnrm = NULL;
    XrVector2f *vuv = NULL;
    XrVector4sFB *vbi = NULL;
    XrVector4f *vbw = NULL;
    float *radii = NULL; /* required capacity, dropped after fetch */
    XrPosef *bpose = NULL; /* bind joint POSES (orientation + position) */
    XrHandJointEXT *bpar = NULL;
    int16_t *idx = NULL;
    DuelHandMesh *dm;
    uint32_t nv, ni;
    uint32_t k;
    if (side < 0 || side > 1) return 0;
    dm = &s_mesh[side];
    mesh_free(dm);
    if (!s_inited || !s_getmesh || !s_tracker[side]) return 0;
    /* Call 1: counts. */
    memset(&m, 0, sizeof m);
    m.type = XR_TYPE_HAND_TRACKING_MESH_FB;
    if (s_getmesh(s_tracker[side], &m) != XR_SUCCESS) return 0;
    if (m.jointCountOutput != DUEL_HAND_JOINTS) return 0;
    nv = m.vertexCountOutput;
    ni = m.indexCountOutput;
    if (nv < 4 || nv > DUEL_MESH_MAX_VERTS) return 0;
    if (ni < 3 || ni > DUEL_MESH_MAX_IDX || (ni % 3) != 0) return 0;
    /* Call 2: fill. */
    vpos = (XrVector3f *)malloc(nv * sizeof *vpos);
    vnrm = (XrVector3f *)malloc(nv * sizeof *vnrm);
    vuv = (XrVector2f *)malloc(nv * sizeof *vuv);
    vbi = (XrVector4sFB *)malloc(nv * sizeof *vbi);
    vbw = (XrVector4f *)malloc(nv * sizeof *vbw);
    radii = (float *)malloc(DUEL_HAND_JOINTS * sizeof *radii);
    bpose = (XrPosef *)malloc(DUEL_HAND_JOINTS * sizeof *bpose);
    bpar = (XrHandJointEXT *)malloc(DUEL_HAND_JOINTS * sizeof *bpar);
    idx = (int16_t *)malloc(ni * sizeof *idx);
    if (!vpos || !vnrm || !vuv || !vbi || !vbw || !radii ||
        !bpose || !bpar || !idx) {
        free(vpos); free(vnrm); free(vuv); free(vbi); free(vbw);
        free(radii); free(bpose); free(bpar); free(idx);
        return 0;
    }
    memset(&m, 0, sizeof m);
    m.type = XR_TYPE_HAND_TRACKING_MESH_FB;
    m.jointCapacityInput = DUEL_HAND_JOINTS;
    m.jointBindPoses = bpose;
    m.jointParents = bpar;
    m.jointRadii = radii;
    m.vertexCapacityInput = nv;
    m.vertexPositions = vpos;
    m.vertexNormals = vnrm;
    m.vertexUVs = vuv;
    m.vertexBlendIndices = vbi;
    m.vertexBlendWeights = vbw;
    m.indexCapacityInput = ni;
    m.indices = idx;
    if (s_getmesh(s_tracker[side], &m) != XR_SUCCESS ||
        m.jointCountOutput != DUEL_HAND_JOINTS ||
        m.vertexCountOutput != nv || m.indexCountOutput != ni) {
        free(vpos); free(vnrm); free(vuv); free(vbi); free(vbw);
        free(radii); free(bpose); free(bpar); free(idx);
        return 0;
    }
    dm->pos = (DuelVec3 *)malloc(nv * sizeof *dm->pos);
    dm->bi = (int16_t *)malloc(nv * 4 * sizeof *dm->bi);
    dm->bw = (float *)malloc(nv * 4 * sizeof *dm->bw);
    dm->bpos = (DuelVec3 *)malloc(DUEL_HAND_JOINTS * sizeof *dm->bpos);
    dm->bquat = (DuelQuat *)malloc(DUEL_HAND_JOINTS * sizeof *dm->bquat);
    dm->idx = (int16_t *)malloc(ni * sizeof *dm->idx);
    if (!dm->pos || !dm->bi || !dm->bw || !dm->bpos || !dm->bquat || !dm->idx) {
        free(vpos); free(vnrm); free(vuv); free(vbi); free(vbw);
        free(radii); free(bpose); free(bpar); free(idx);
        mesh_free(dm);
        return 0;
    }
    for (k = 0; k < nv; k++) {
        dm->pos[k] = duel_v3(vpos[k].x, vpos[k].y, vpos[k].z);
        dm->bi[k * 4 + 0] = vbi[k].x;
        dm->bi[k * 4 + 1] = vbi[k].y;
        dm->bi[k * 4 + 2] = vbi[k].z;
        dm->bi[k * 4 + 3] = vbi[k].w;
        dm->bw[k * 4 + 0] = vbw[k].x;
        dm->bw[k * 4 + 1] = vbw[k].y;
        dm->bw[k * 4 + 2] = vbw[k].z;
        dm->bw[k * 4 + 3] = vbw[k].w;
        /* Paranoia: clamp blend refs into the joint set. */
        {
            int q;
            for (q = 0; q < 4; q++)
                if (dm->bi[k * 4 + q] < 0 ||
                    dm->bi[k * 4 + q] >= DUEL_HAND_JOINTS)
                    dm->bi[k * 4 + q] = 0;
        }
    }
    for (k = 0; k < (uint32_t)DUEL_HAND_JOINTS; k++) {
        dm->bpos[k] = duel_v3(bpose[k].position.x, bpose[k].position.y,
                              bpose[k].position.z);
        dm->bquat[k] = duel_q(bpose[k].orientation.x, bpose[k].orientation.y,
                              bpose[k].orientation.z, bpose[k].orientation.w);
    }
    for (k = 0; k < ni; k++) {
        int v = (int)idx[k];
        if (v < 0 || v >= (int)nv) v = 0;
        dm->idx[k] = (int16_t)v;
    }
    free(vpos); free(vnrm); free(vuv); free(vbi); free(vbw);
    free(radii); free(bpose); free(bpar); free(idx);
    dm->n_verts = (int)nv;
    dm->n_idx = (int)ni;
    dm->have = 1;
    return 1;
}

void DuelHands_Local2Game(const DuelSeatCal *cal,
                           DuelQuat ship_q, DuelVec3 ship_t, DuelVec3 seat_t,
                           DuelVec3 mo_p, DuelQuat mo_q, float out16[16]) {
    DuelMat4 M = duel_m4_rigid(ship_q, ship_t);
    DuelMat4 mo = duel_m4_rigid(mo_q, mo_p);
    DuelMat4 st = duel_m4_rigid(duel_q_identity(), seat_t);
    M = duel_m4_mul(M, mo);
    M = duel_m4_mul(M, st);
    if (cal && cal->valid) {
        DuelQuat yc_q = duel_q_axis_angle(duel_v3(0, 1, 0), -cal->neutral_yaw);
        DuelMat4 yc = duel_m4_rigid(yc_q, duel_v3(0, 0, 0));
        DuelMat4 nt = duel_m4_rigid(duel_q_identity(),
                                    duel_v3(-cal->neutral_pos.x,
                                            -cal->neutral_pos.y,
                                            -cal->neutral_pos.z));
        M = duel_m4_mul(M, duel_m4_mul(yc, nt));
    }
    memcpy(out16, M.m, sizeof M.m);
}

void DuelSkin_JointMats(const DuelHand *h, const DuelHandMesh *m,
                        DuelMat4 out[26]) {
    int j;
    float s = (m && m->scale > 1e-6f) ? m->scale : 1.0f;
    for (j = 0; j < 26; j++) {
        DuelVec3 bp;
        DuelMat4 W;
        if (!h || !m || !m->have) {
            out[j] = duel_m4_rigid(duel_q_identity(), duel_v3(0, 0, 0));
            continue;
        }
        bp = duel_v3(m->bpos[j].x * s, m->bpos[j].y * s, m->bpos[j].z * s);
        W = duel_m4_rigid(h->j[j].quat, h->j[j].pos);
        out[j] = duel_m4_mul(W, duel_m4_rigid_inv(m->bquat[j], bp));
    }
}

void DuelSkin_GameMats(const float game16[16], const DuelMat4 jm[26],
                       DuelMat4 out[26]) {
    DuelMat4 G;
    int j;
    memcpy(G.m, game16, sizeof G.m);
    for (j = 0; j < 26; j++)
        out[j] = duel_m4_mul(G, jm[j]);
}

void DuelSkin_Vert(DuelVec3 bind, const int16_t bi[4], const float bw[4],
                   const DuelMat4 gj[26], const int jvalid[26],
                   DuelVec3 *out) {
    float wsum = 0.0f;
    DuelVec3 acc = duel_v3(0.0f, 0.0f, 0.0f);
    int k;
    for (k = 0; k < 4; k++) {
        int j = (int)bi[k];
        if (j < 0 || j >= 26) continue;
        if (bw[k] > 0.0f && (!jvalid || jvalid[j]))
            wsum += bw[k];
    }
    if (wsum < 1e-6f) {
        *out = duel_m4_xform_point(gj[1], bind); /* wrist: stay attached */
        return;
    }
    for (k = 0; k < 4; k++) {
        int j = (int)bi[k];
        DuelVec3 p;
        if (j < 0 || j >= 26) continue;
        if (bw[k] <= 0.0f || (jvalid && !jvalid[j])) continue;
        p = duel_m4_xform_point(gj[j], bind);
        acc.x += p.x * bw[k] / wsum;
        acc.y += p.y * bw[k] / wsum;
        acc.z += p.z * bw[k] / wsum;
    }
    *out = acc;
}

void DuelGrab_Reset(DuelGrab *g) {
    g->state = DUEL_GRAB_FAR;
    g->hold_frames = 0;
    g->rel_frames = 0;
}

void DuelGrab_Update(DuelGrab *g, int active, float palm_dist,
                     float pinch_dist, float mid_dist) {
    DuelGrab_UpdateT(g, active, palm_dist, pinch_dist, mid_dist,
                     DUEL_GRAB_PINCH_SHUT, DUEL_GRAB_PINCH_OPEN,
                     DUEL_GRAB_GRAB_R, DUEL_GRAB_LOSE_R);
}

void DuelGrab_UpdateT(DuelGrab *g, int active, float palm_dist,
                      float grip_dist, float mid_dist,
                      float shut_th, float open_th,
                      float grab_r, float lose_r) {
    int shut;
    int open;
    if (!active) {
        /* Tracking lost: drop everything immediately (a held lever must
         * never stick on stale data). */
        DuelGrab_Reset(g);
        return;
    }
    shut = grip_dist <= shut_th;
    open = grip_dist >= open_th;
    switch (g->state) {
    case DUEL_GRAB_FAR:
        g->hold_frames = 0;
        g->rel_frames = 0;
        if (palm_dist <= DUEL_GRAB_NEAR_R)
            g->state = DUEL_GRAB_NEAR;
        break;
    case DUEL_GRAB_NEAR:
        if (palm_dist >= DUEL_GRAB_FAR_R) {
            DuelGrab_Reset(g);
        } else if (shut && mid_dist <= grab_r) {
            if (++g->hold_frames >= DUEL_GRAB_HOLD_FRAMES) {
                g->state = DUEL_GRAB_HELD;
                g->hold_frames = 0;
                g->rel_frames = 0;
            }
        } else {
            g->hold_frames = 0;
        }
        break;
    case DUEL_GRAB_HELD:
        g->hold_frames = 0;
        if (mid_dist >= lose_r) {
            /* yanked away: instant release (no debounce on leave). */
            DuelGrab_Reset(g);
        } else if (open) {
            if (++g->rel_frames >= DUEL_GRAB_REL_FRAMES) {
                g->state = DUEL_GRAB_NEAR;
                g->rel_frames = 0;
            }
        } else {
            g->rel_frames = 0;
        }
        break;
    }
}

int DuelLever_IsPrim(int mesh_idx, int prim_idx) {
    return mesh_idx == DUEL_LEVER_MESH &&
        prim_idx >= DUEL_LEVER_P0 && prim_idx <= DUEL_LEVER_P1;
}

void DuelStick_Defl(DuelVec3 mid_ship, float *pd, float *rd) {
    /* Inverted-real: toward the pilot (+Z ship) = nose up; right = roll. */
    *pd = duel_clampf((mid_ship.z - DUEL_LEVER_SHIP.z) / DUEL_STICK_RANGE,
                      -1.0f, 1.0f);
    *rd = duel_clampf((mid_ship.x - DUEL_LEVER_SHIP.x) / DUEL_STICK_RANGE,
                      -1.0f, 1.0f);
}

void DuelStick_DeflRel(DuelVec3 palm_ship, DuelVec3 anchor_ship,
                       float *pd, float *rd) {
    /* Same feel, latch-relative: push from where the palm grabbed. */
    *pd = duel_clampf((palm_ship.z - anchor_ship.z) / DUEL_STICK_RANGE,
                      -1.0f, 1.0f);
    *rd = duel_clampf((palm_ship.x - anchor_ship.x) / DUEL_STICK_RANGE,
                      -1.0f, 1.0f);
}

float DuelHand_FistDist(const DuelHand *h) {
    static const int tips[4] = { 10, 15, 20, 25 };
    float worst = 0.0f;
    int nvalid = 0, k;
    if (!h || !h->j[0].pos_valid) return 99.0f;
    for (k = 0; k < 4; k++) {
        const DuelHandJoint *j = &h->j[tips[k]];
        float d;
        if (!j->pos_valid) continue;
        nvalid++;
        d = duel_v3_len(duel_v3_sub(j->pos, h->j[0].pos));
        if (d > worst) worst = d;
    }
    if (nvalid < DUEL_FIST_MIN_VALID) return 99.0f;
    return worst;
}

void DuelStick_GripShip(float pd, float rd, DuelVec3 *out) {
    DuelVec3 rel = duel_v3_sub(DUEL_LEVER_SHIP, DUEL_LEVER_PIVOT_SHIP);
    DuelQuat rx = duel_q_axis_angle(duel_v3(1, 0, 0), DUEL_LEVER_MAX_A * pd);
    DuelQuat rz = duel_q_axis_angle(duel_v3(0, 0, 1), -DUEL_LEVER_MAX_A * rd);
    DuelQuat r = duel_q_norm(duel_q_mul(rx, rz));
    *out = duel_v3_add(DUEL_LEVER_PIVOT_SHIP, duel_q_rot(r, rel));
}

void DuelStick_Decay(float *pd, float *rd, float dt) {
    /* Exponential spring return; snap micro-residue to rest. */
    float k = 1.0f - dt / DUEL_STICK_TAU;
    if (k < 0.0f) k = 0.0f;
    *pd *= k;
    *rd *= k;
    if (*pd > -1e-3f && *pd < 1e-3f) *pd = 0.0f;
    if (*rd > -1e-3f && *rd < 1e-3f) *rd = 0.0f;
}

void DuelStick_ModelOffset(float pd, float rd, float out16[16]) {
    /* OPT-frame conjugate of ship Rx(a)Rz(b), a=+A*pd, b=-A*rd:
     * Ry(PI) Rx(a) Rz(b) Ry(PI) = Rx(-a) Rz(-b). */
    DuelQuat rx = duel_q_axis_angle(duel_v3(1, 0, 0), -DUEL_LEVER_MAX_A * pd);
    DuelQuat rz = duel_q_axis_angle(duel_v3(0, 0, 1), DUEL_LEVER_MAX_A * rd);
    DuelQuat r = duel_q_norm(duel_q_mul(rx, rz));
    DuelVec3 pm = DUEL_LEVER_PIVOT_MODEL;
    DuelVec3 np = duel_v3(-pm.x, -pm.y, -pm.z);
    DuelMat4 t1 = duel_m4_rigid(duel_q_identity(), pm);
    DuelMat4 rr = duel_m4_rigid(r, duel_v3(0, 0, 0));
    DuelMat4 t0 = duel_m4_rigid(duel_q_identity(), np);
    DuelMat4 m = duel_m4_mul(t1, duel_m4_mul(rr, t0));
    int i;
    for (i = 0; i < 16; i++) out16[i] = m.m[i];
}

void DuelQuad_GripShip(float throttle, DuelVec3 *out) {
    float t = duel_clampf(throttle, 0.0f, 1.0f);
    /* Friction slide along the slot: 0 back (+Z), 1 forward (-Z). */
    *out = duel_v3(DUEL_QUAD_SHIP.x, DUEL_QUAD_SHIP.y,
                   DUEL_QUAD_SHIP.z + DUEL_QUAD_TRAVEL * (1.0f - 2.0f * t));
}

float DuelQuad_ThrottleForMid(float mid_z) {
    float back = DUEL_QUAD_SHIP.z + DUEL_QUAD_TRAVEL;
    float fwd = DUEL_QUAD_SHIP.z - DUEL_QUAD_TRAVEL;
    return duel_clampf((back - mid_z) / (back - fwd), 0.0f, 1.0f);
}

void DuelLever_GripShip(float throttle, float roll, DuelVec3 *out) {
    DuelQuad_GripShip(throttle, out);
    /* Lateral swing tracks the full-deflection hand travel exactly, so
     * the grip stays under the palm while held (lose radius is 20 cm;
     * without follow, deep rolls would still hold, but tight is better
     * for re-latch feel). Sign matches DeflRel: push left = -x. */
    out->x += duel_clampf(roll, -1.0f, 1.0f) * DUEL_STICK_RANGE;
}

void DuelFire_Reset(DuelFire *f) {
    f->firing = 0;
    f->on_n = 0;
    f->off_n = 0;
}

void DuelFire_Update(DuelFire *f, int held, int valid, float dist) {
    if (!held || !valid) {
        /* Released the stick (or lost joints): trigger goes dead now. */
        DuelFire_Reset(f);
        return;
    }
    /* M3.8 touch-fire: CLOSE latches (thumb on the index), FAR
     * releases (thumb peeled away). Mirror of the old reach logic. */
    if (!f->firing) {
        if (dist <= DUEL_FIRE_ON_DIST) {
            if (++f->on_n >= DUEL_FIRE_ON_FRAMES) {
                f->firing = 1;
                f->on_n = 0;
            }
        } else {
            f->on_n = 0;
        }
    } else {
        if (dist >= DUEL_FIRE_OFF_DIST) {
            if (++f->off_n >= DUEL_FIRE_OFF_FRAMES) {
                f->firing = 0;
                f->off_n = 0;
            }
        } else {
            f->off_n = 0;
        }
    }
}

int DuelFire_HoldOk(int grab_held, int active, int index_valid,
                    float index_ext) {
    return grab_held && active && index_valid &&
        index_ext <= DUEL_FIRE_INDEX_MAX;
}

void DuelGrab_Assign(const DuelGrab g[2][2], const float dist[2][2],
                     int holder[2]) {
    int hand_tgt[2] = { -1, -1 };
    int h, t;
    for (h = 0; h < 2; h++) {
        int best = -1;
        for (t = 0; t < 2; t++) {
            if (g[h][t].state != DUEL_GRAB_HELD) continue;
            if (best < 0 || dist[h][t] < dist[h][best]) best = t;
        }
        hand_tgt[h] = best;
    }
    for (t = 0; t < 2; t++) {
        int win = -1;
        for (h = 0; h < 2; h++) {
            if (hand_tgt[h] != t) continue;
            if (win < 0 || dist[h][t] < dist[win][t]) win = h;
        }
        holder[t] = win;
    }
}

/* M10 touch menus: press level with hysteresis (pure, tested). */
void DuelTouch_Level(int *pressed, float dist, int in_bounds) {
    int p;
    if (!pressed) return;
    p = *pressed ? 1 : 0;
    if (!in_bounds || dist > DUEL_TOUCH_RELEASE_D) p = 0;
    else if (dist <= DUEL_TOUCH_PRESS_D && dist >= DUEL_TOUCH_BACK_D) p = 1;
    *pressed = p;
}

void DuelPanelGrab_Reset(DuelPanelGrab *g) {
    g->held = 0;
}

void DuelPanelGrab_Update(DuelPanelGrab *g, int active, int near, float pinch) {
    if (!active) {
        /* Tracking lost (or cal invalid / other hand holds): drop now,
         * like the HOTAS machines, never stick on stale data. */
        g->held = 0;
        return;
    }
    if (g->held) {
        if (pinch >= DUEL_PANEL_PINCH_OPEN) g->held = 0;
    } else if (near && pinch <= DUEL_PANEL_PINCH_SHUT) {
        g->held = 1;
    }
}
