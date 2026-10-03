// D3DX9 math for the native build (plan Phase 3, port/d3dx): the 35 functions the client
// links. Conventions are D3DX's: row vectors (v * M), left-handed, D3DXQuaternionMultiply(q1, q2)
// = "rotate by q1, then by q2". Outputs may alias inputs, as with D3DX. Each function is pinned
// by port/tests/d3dx_math_test.cpp.
#include "ran_compat.h"
#include <d3dx9math.h>
#include <cmath>

namespace {
inline float Dot3(const D3DXVECTOR3& a, const D3DXVECTOR3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline D3DXVECTOR3 Cross3(const D3DXVECTOR3& a, const D3DXVECTOR3& b)
{
    return D3DXVECTOR3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
inline D3DXVECTOR3 Norm3(const D3DXVECTOR3& v)
{
    const float len = std::sqrt(Dot3(v, v));
    return len != 0.0f ? D3DXVECTOR3(v.x / len, v.y / len, v.z / len) : D3DXVECTOR3(0.0f, 0.0f, 0.0f);
}
inline float DotQ(const D3DXQUATERNION& a, const D3DXQUATERNION& b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
inline void Identity(D3DXMATRIX* m)
{
    for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) m->m[r][c] = r == c ? 1.0f : 0.0f;
}
// Unit-quaternion logarithm / exponential (used by SquadSetup).
inline D3DXQUATERNION QLn(const D3DXQUATERNION& q)
{
    const float w = q.w > 1.0f ? 1.0f : (q.w < -1.0f ? -1.0f : q.w);
    const float theta = std::acos(w), s = std::sin(theta);
    if (std::fabs(s) < 1e-6f) return D3DXQUATERNION(q.x, q.y, q.z, 0.0f);
    const float k = theta / s;
    return D3DXQUATERNION(q.x * k, q.y * k, q.z * k, 0.0f);
}
inline D3DXQUATERNION QExp(const D3DXQUATERNION& q)
{
    const float theta = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z);
    if (theta < 1e-6f) return D3DXQUATERNION(q.x, q.y, q.z, std::cos(theta));
    const float k = std::sin(theta) / theta;
    return D3DXQUATERNION(q.x * k, q.y * k, q.z * k, std::cos(theta));
}
} // namespace

extern "C" {

// ---- Vectors
D3DXVECTOR2* WINAPI D3DXVec2Normalize(D3DXVECTOR2* pOut, CONST D3DXVECTOR2* pV)
{
    const float len = std::sqrt(pV->x * pV->x + pV->y * pV->y);
    if (len == 0.0f) { pOut->x = pOut->y = 0.0f; return pOut; }
    const D3DXVECTOR2 v = *pV;
    pOut->x = v.x / len; pOut->y = v.y / len;
    return pOut;
}

D3DXVECTOR3* WINAPI D3DXVec3Normalize(D3DXVECTOR3* pOut, CONST D3DXVECTOR3* pV)
{
    *pOut = Norm3(*pV);
    return pOut;
}

D3DXVECTOR4* WINAPI D3DXVec4Normalize(D3DXVECTOR4* pOut, CONST D3DXVECTOR4* pV)
{
    const D3DXVECTOR4 v = *pV;
    const float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z + v.w * v.w);
    if (len == 0.0f) { *pOut = D3DXVECTOR4(0.0f, 0.0f, 0.0f, 0.0f); return pOut; }
    *pOut = D3DXVECTOR4(v.x / len, v.y / len, v.z / len, v.w / len);
    return pOut;
}

D3DXVECTOR3* WINAPI D3DXVec3CatmullRom(D3DXVECTOR3* pOut, CONST D3DXVECTOR3* pV0, CONST D3DXVECTOR3* pV1,
                                       CONST D3DXVECTOR3* pV2, CONST D3DXVECTOR3* pV3, FLOAT s)
{
    const D3DXVECTOR3 v0 = *pV0, v1 = *pV1, v2 = *pV2, v3 = *pV3;
    const float s2 = s * s, s3 = s2 * s;
    *pOut = 0.5f * (2.0f * v1 + (v2 - v0) * s + (2.0f * v0 - 5.0f * v1 + 4.0f * v2 - v3) * s2
                    + (3.0f * v1 - v0 - 3.0f * v2 + v3) * s3);
    return pOut;
}

D3DXVECTOR4* WINAPI D3DXVec4Transform(D3DXVECTOR4* pOut, CONST D3DXVECTOR4* pV, CONST D3DXMATRIX* pM)
{
    const D3DXVECTOR4 v = *pV;
    const D3DXMATRIX& m = *pM;
    *pOut = D3DXVECTOR4(v.x * m._11 + v.y * m._21 + v.z * m._31 + v.w * m._41,
                        v.x * m._12 + v.y * m._22 + v.z * m._32 + v.w * m._42,
                        v.x * m._13 + v.y * m._23 + v.z * m._33 + v.w * m._43,
                        v.x * m._14 + v.y * m._24 + v.z * m._34 + v.w * m._44);
    return pOut;
}

D3DXVECTOR3* WINAPI D3DXVec3TransformCoord(D3DXVECTOR3* pOut, CONST D3DXVECTOR3* pV, CONST D3DXMATRIX* pM)
{
    const D3DXVECTOR3 v = *pV;
    const D3DXMATRIX& m = *pM;
    const float w = v.x * m._14 + v.y * m._24 + v.z * m._34 + m._44;
    const float inv = w != 0.0f ? 1.0f / w : 0.0f;
    *pOut = D3DXVECTOR3((v.x * m._11 + v.y * m._21 + v.z * m._31 + m._41) * inv,
                        (v.x * m._12 + v.y * m._22 + v.z * m._32 + m._42) * inv,
                        (v.x * m._13 + v.y * m._23 + v.z * m._33 + m._43) * inv);
    return pOut;
}

D3DXVECTOR3* WINAPI D3DXVec3TransformNormal(D3DXVECTOR3* pOut, CONST D3DXVECTOR3* pV, CONST D3DXMATRIX* pM)
{
    const D3DXVECTOR3 v = *pV;
    const D3DXMATRIX& m = *pM;
    *pOut = D3DXVECTOR3(v.x * m._11 + v.y * m._21 + v.z * m._31,
                        v.x * m._12 + v.y * m._22 + v.z * m._32,
                        v.x * m._13 + v.y * m._23 + v.z * m._33);
    return pOut;
}

D3DXVECTOR3* WINAPI D3DXVec3Project(D3DXVECTOR3* pOut, CONST D3DXVECTOR3* pV, CONST D3DVIEWPORT9* pViewport,
                                    CONST D3DXMATRIX* pProjection, CONST D3DXMATRIX* pView, CONST D3DXMATRIX* pWorld)
{
    D3DXMATRIX m;
    Identity(&m);
    if (pWorld) D3DXMatrixMultiply(&m, &m, pWorld);
    if (pView) D3DXMatrixMultiply(&m, &m, pView);
    if (pProjection) D3DXMatrixMultiply(&m, &m, pProjection);
    D3DXVECTOR3 out;
    D3DXVec3TransformCoord(&out, pV, &m);
    if (pViewport) {
        out.x = pViewport->X + (1.0f + out.x) * pViewport->Width / 2.0f;
        out.y = pViewport->Y + (1.0f - out.y) * pViewport->Height / 2.0f;
        out.z = pViewport->MinZ + out.z * (pViewport->MaxZ - pViewport->MinZ);
    }
    *pOut = out;
    return pOut;
}

// ---- Matrices
D3DXMATRIX* WINAPI D3DXMatrixMultiply(D3DXMATRIX* pOut, CONST D3DXMATRIX* pM1, CONST D3DXMATRIX* pM2)
{
    D3DXMATRIX r;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            r.m[i][j] = pM1->m[i][0] * pM2->m[0][j] + pM1->m[i][1] * pM2->m[1][j]
                      + pM1->m[i][2] * pM2->m[2][j] + pM1->m[i][3] * pM2->m[3][j];
    *pOut = r;
    return pOut;
}

D3DXMATRIX* WINAPI D3DXMatrixTranspose(D3DXMATRIX* pOut, CONST D3DXMATRIX* pM)
{
    D3DXMATRIX r;
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) r.m[i][j] = pM->m[j][i];
    *pOut = r;
    return pOut;
}

D3DXMATRIX* WINAPI D3DXMatrixInverse(D3DXMATRIX* pOut, FLOAT* pDeterminant, CONST D3DXMATRIX* pM)
{
    const float* m = &pM->m[0][0];
    float inv[16];
    inv[0]  =  m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
    inv[4]  = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
    inv[8]  =  m[4]*m[9]*m[15]  - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
    inv[12] = -m[4]*m[9]*m[14]  + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
    inv[1]  = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
    inv[5]  =  m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
    inv[9]  = -m[0]*m[9]*m[15]  + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
    inv[13] =  m[0]*m[9]*m[14]  - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
    inv[2]  =  m[1]*m[6]*m[15]  - m[1]*m[7]*m[14]  - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7]  - m[13]*m[3]*m[6];
    inv[6]  = -m[0]*m[6]*m[15]  + m[0]*m[7]*m[14]  + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7]  + m[12]*m[3]*m[6];
    inv[10] =  m[0]*m[5]*m[15]  - m[0]*m[7]*m[13]  - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7]  - m[12]*m[3]*m[5];
    inv[14] = -m[0]*m[5]*m[14]  + m[0]*m[6]*m[13]  + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6]  + m[12]*m[2]*m[5];
    inv[3]  = -m[1]*m[6]*m[11]  + m[1]*m[7]*m[10]  + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7]   + m[9]*m[3]*m[6];
    inv[7]  =  m[0]*m[6]*m[11]  - m[0]*m[7]*m[10]  - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7]   - m[8]*m[3]*m[6];
    inv[11] = -m[0]*m[5]*m[11]  + m[0]*m[7]*m[9]   + m[4]*m[1]*m[11] - m[4]*m[3]*m[9]  - m[8]*m[1]*m[7]   + m[8]*m[3]*m[5];
    inv[15] =  m[0]*m[5]*m[10]  - m[0]*m[6]*m[9]   - m[4]*m[1]*m[10] + m[4]*m[2]*m[9]  + m[8]*m[1]*m[6]   - m[8]*m[2]*m[5];
    const float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (pDeterminant) *pDeterminant = det;
    if (det == 0.0f) return nullptr;
    const float invDet = 1.0f / det;
    for (int i = 0; i < 16; ++i) (&pOut->m[0][0])[i] = inv[i] * invDet;
    return pOut;
}

D3DXMATRIX* WINAPI D3DXMatrixTranslation(D3DXMATRIX* pOut, FLOAT x, FLOAT y, FLOAT z)
{
    Identity(pOut);
    pOut->_41 = x; pOut->_42 = y; pOut->_43 = z;
    return pOut;
}

D3DXMATRIX* WINAPI D3DXMatrixScaling(D3DXMATRIX* pOut, FLOAT sx, FLOAT sy, FLOAT sz)
{
    Identity(pOut);
    pOut->_11 = sx; pOut->_22 = sy; pOut->_33 = sz;
    return pOut;
}

D3DXMATRIX* WINAPI D3DXMatrixRotationX(D3DXMATRIX* pOut, FLOAT a)
{
    Identity(pOut);
    const float c = std::cos(a), s = std::sin(a);
    pOut->_22 = c; pOut->_23 = s; pOut->_32 = -s; pOut->_33 = c;
    return pOut;
}

D3DXMATRIX* WINAPI D3DXMatrixRotationY(D3DXMATRIX* pOut, FLOAT a)
{
    Identity(pOut);
    const float c = std::cos(a), s = std::sin(a);
    pOut->_11 = c; pOut->_13 = -s; pOut->_31 = s; pOut->_33 = c;
    return pOut;
}

D3DXMATRIX* WINAPI D3DXMatrixRotationZ(D3DXMATRIX* pOut, FLOAT a)
{
    Identity(pOut);
    const float c = std::cos(a), s = std::sin(a);
    pOut->_11 = c; pOut->_12 = s; pOut->_21 = -s; pOut->_22 = c;
    return pOut;
}

D3DXMATRIX* WINAPI D3DXMatrixRotationAxis(D3DXMATRIX* pOut, CONST D3DXVECTOR3* pV, FLOAT a)
{
    const D3DXVECTOR3 v = Norm3(*pV);
    const float c = std::cos(a), s = std::sin(a), t = 1.0f - c;
    Identity(pOut);
    pOut->_11 = t * v.x * v.x + c;       pOut->_12 = t * v.x * v.y + s * v.z; pOut->_13 = t * v.x * v.z - s * v.y;
    pOut->_21 = t * v.x * v.y - s * v.z; pOut->_22 = t * v.y * v.y + c;       pOut->_23 = t * v.y * v.z + s * v.x;
    pOut->_31 = t * v.x * v.z + s * v.y; pOut->_32 = t * v.y * v.z - s * v.x; pOut->_33 = t * v.z * v.z + c;
    return pOut;
}

D3DXMATRIX* WINAPI D3DXMatrixRotationQuaternion(D3DXMATRIX* pOut, CONST D3DXQUATERNION* pQ)
{
    const D3DXQUATERNION q = *pQ;
    Identity(pOut);
    pOut->_11 = 1.0f - 2.0f * (q.y * q.y + q.z * q.z);
    pOut->_12 = 2.0f * (q.x * q.y + q.z * q.w);
    pOut->_13 = 2.0f * (q.x * q.z - q.y * q.w);
    pOut->_21 = 2.0f * (q.x * q.y - q.z * q.w);
    pOut->_22 = 1.0f - 2.0f * (q.x * q.x + q.z * q.z);
    pOut->_23 = 2.0f * (q.y * q.z + q.x * q.w);
    pOut->_31 = 2.0f * (q.x * q.z + q.y * q.w);
    pOut->_32 = 2.0f * (q.y * q.z - q.x * q.w);
    pOut->_33 = 1.0f - 2.0f * (q.x * q.x + q.y * q.y);
    return pOut;
}

// Roll about Z, then pitch about X, then yaw about Y.
D3DXMATRIX* WINAPI D3DXMatrixRotationYawPitchRoll(D3DXMATRIX* pOut, FLOAT yaw, FLOAT pitch, FLOAT roll)
{
    D3DXMATRIX mz, mx, my;
    D3DXMatrixRotationZ(&mz, roll);
    D3DXMatrixRotationX(&mx, pitch);
    D3DXMatrixRotationY(&my, yaw);
    D3DXMatrixMultiply(pOut, &mz, &mx);
    D3DXMatrixMultiply(pOut, pOut, &my);
    return pOut;
}

D3DXMATRIX* WINAPI D3DXMatrixLookAtLH(D3DXMATRIX* pOut, CONST D3DXVECTOR3* pEye, CONST D3DXVECTOR3* pAt, CONST D3DXVECTOR3* pUp)
{
    const D3DXVECTOR3 eye = *pEye;
    const D3DXVECTOR3 z = Norm3(*pAt - eye);
    const D3DXVECTOR3 x = Norm3(Cross3(*pUp, z));
    const D3DXVECTOR3 y = Cross3(z, x);
    Identity(pOut);
    pOut->_11 = x.x; pOut->_12 = y.x; pOut->_13 = z.x;
    pOut->_21 = x.y; pOut->_22 = y.y; pOut->_23 = z.y;
    pOut->_31 = x.z; pOut->_32 = y.z; pOut->_33 = z.z;
    pOut->_41 = -Dot3(x, eye); pOut->_42 = -Dot3(y, eye); pOut->_43 = -Dot3(z, eye);
    return pOut;
}

D3DXMATRIX* WINAPI D3DXMatrixPerspectiveFovLH(D3DXMATRIX* pOut, FLOAT fovy, FLOAT aspect, FLOAT zn, FLOAT zf)
{
    const float yScale = 1.0f / std::tan(fovy / 2.0f);
    Identity(pOut);
    pOut->_11 = yScale / aspect;
    pOut->_22 = yScale;
    pOut->_33 = zf / (zf - zn);
    pOut->_34 = 1.0f;
    pOut->_43 = -zn * zf / (zf - zn);
    pOut->_44 = 0.0f;
    return pOut;
}

D3DXMATRIX* WINAPI D3DXMatrixOrthoLH(D3DXMATRIX* pOut, FLOAT w, FLOAT h, FLOAT zn, FLOAT zf)
{
    Identity(pOut);
    pOut->_11 = 2.0f / w;
    pOut->_22 = 2.0f / h;
    pOut->_33 = 1.0f / (zf - zn);
    pOut->_43 = zn / (zn - zf);
    return pOut;
}

// ---- Planes
D3DXPLANE* WINAPI D3DXPlaneFromPointNormal(D3DXPLANE* pOut, CONST D3DXVECTOR3* pPoint, CONST D3DXVECTOR3* pNormal)
{
    const D3DXVECTOR3 p = *pPoint, n = *pNormal;
    pOut->a = n.x; pOut->b = n.y; pOut->c = n.z;
    pOut->d = -Dot3(p, n);
    return pOut;
}

D3DXPLANE* WINAPI D3DXPlaneFromPoints(D3DXPLANE* pOut, CONST D3DXVECTOR3* pV1, CONST D3DXVECTOR3* pV2, CONST D3DXVECTOR3* pV3)
{
    const D3DXVECTOR3 p1 = *pV1;
    const D3DXVECTOR3 n = Norm3(Cross3(*pV2 - p1, *pV3 - p1));
    return D3DXPlaneFromPointNormal(pOut, &p1, &n);
}

D3DXVECTOR3* WINAPI D3DXPlaneIntersectLine(D3DXVECTOR3* pOut, CONST D3DXPLANE* pP, CONST D3DXVECTOR3* pV1, CONST D3DXVECTOR3* pV2)
{
    const D3DXVECTOR3 v1 = *pV1, dir = *pV2 - v1;
    const D3DXVECTOR3 n(pP->a, pP->b, pP->c);
    const float dot = Dot3(n, dir);
    if (dot == 0.0f) return nullptr;   // parallel: no intersection, pOut untouched (as D3DX)
    const float t = (pP->d + Dot3(n, v1)) / dot;
    *pOut = v1 - t * dir;
    return pOut;
}

// The plane as a 4-vector times M (callers pass the inverse transpose, as with D3DX).
D3DXPLANE* WINAPI D3DXPlaneTransform(D3DXPLANE* pOut, CONST D3DXPLANE* pP, CONST D3DXMATRIX* pM)
{
    const D3DXPLANE p = *pP;
    const D3DXMATRIX& m = *pM;
    pOut->a = m._11 * p.a + m._21 * p.b + m._31 * p.c + m._41 * p.d;
    pOut->b = m._12 * p.a + m._22 * p.b + m._32 * p.c + m._42 * p.d;
    pOut->c = m._13 * p.a + m._23 * p.b + m._33 * p.c + m._43 * p.d;
    pOut->d = m._14 * p.a + m._24 * p.b + m._34 * p.c + m._44 * p.d;
    return pOut;
}

// ---- Quaternions
D3DXQUATERNION* WINAPI D3DXQuaternionNormalize(D3DXQUATERNION* pOut, CONST D3DXQUATERNION* pQ)
{
    const D3DXQUATERNION q = *pQ;
    const float len = std::sqrt(DotQ(q, q));
    *pOut = len != 0.0f ? D3DXQUATERNION(q.x / len, q.y / len, q.z / len, q.w / len) : D3DXQUATERNION(0, 0, 0, 0);
    return pOut;
}

D3DXQUATERNION* WINAPI D3DXQuaternionInverse(D3DXQUATERNION* pOut, CONST D3DXQUATERNION* pQ)
{
    const D3DXQUATERNION q = *pQ;
    const float n = DotQ(q, q);
    *pOut = n != 0.0f ? D3DXQUATERNION(-q.x / n, -q.y / n, -q.z / n, q.w / n) : D3DXQUATERNION(0, 0, 0, 0);
    return pOut;
}

// D3DX order: the result rotates by Q1 first, then by Q2 (i.e. the Hamilton product Q2*Q1).
D3DXQUATERNION* WINAPI D3DXQuaternionMultiply(D3DXQUATERNION* pOut, CONST D3DXQUATERNION* pQ1, CONST D3DXQUATERNION* pQ2)
{
    const D3DXQUATERNION a = *pQ1, b = *pQ2;
    *pOut = D3DXQUATERNION(b.w * a.x + b.x * a.w + b.y * a.z - b.z * a.y,
                           b.w * a.y - b.x * a.z + b.y * a.w + b.z * a.x,
                           b.w * a.z + b.x * a.y - b.y * a.x + b.z * a.w,
                           b.w * a.w - b.x * a.x - b.y * a.y - b.z * a.z);
    return pOut;
}

D3DXQUATERNION* WINAPI D3DXQuaternionRotationAxis(D3DXQUATERNION* pOut, CONST D3DXVECTOR3* pV, FLOAT a)
{
    const D3DXVECTOR3 v = Norm3(*pV);
    const float s = std::sin(a / 2.0f);
    *pOut = D3DXQUATERNION(v.x * s, v.y * s, v.z * s, std::cos(a / 2.0f));
    return pOut;
}

D3DXQUATERNION* WINAPI D3DXQuaternionRotationYawPitchRoll(D3DXQUATERNION* pOut, FLOAT yaw, FLOAT pitch, FLOAT roll)
{
    const float sy = std::sin(yaw / 2), cy = std::cos(yaw / 2);
    const float sp = std::sin(pitch / 2), cp = std::cos(pitch / 2);
    const float sr = std::sin(roll / 2), cr = std::cos(roll / 2);
    *pOut = D3DXQUATERNION(sy * cp * sr + cy * sp * cr,
                           sy * cp * cr - cy * sp * sr,
                           cy * cp * sr - sy * sp * cr,
                           cy * cp * cr + sy * sp * sr);
    return pOut;
}

D3DXQUATERNION* WINAPI D3DXQuaternionRotationMatrix(D3DXQUATERNION* pOut, CONST D3DXMATRIX* pM)
{
    const D3DXMATRIX& m = *pM;
    const float trace = m._11 + m._22 + m._33 + 1.0f;
    D3DXQUATERNION q;
    if (trace > 1.0f) {
        const float s = 2.0f * std::sqrt(trace);
        q = D3DXQUATERNION((m._23 - m._32) / s, (m._31 - m._13) / s, (m._12 - m._21) / s, 0.25f * s);
    } else if (m._11 > m._22 && m._11 > m._33) {
        const float s = 2.0f * std::sqrt(1.0f + m._11 - m._22 - m._33);
        q = D3DXQUATERNION(0.25f * s, (m._12 + m._21) / s, (m._13 + m._31) / s, (m._23 - m._32) / s);
    } else if (m._22 > m._33) {
        const float s = 2.0f * std::sqrt(1.0f + m._22 - m._11 - m._33);
        q = D3DXQUATERNION((m._12 + m._21) / s, 0.25f * s, (m._23 + m._32) / s, (m._31 - m._13) / s);
    } else {
        const float s = 2.0f * std::sqrt(1.0f + m._33 - m._11 - m._22);
        q = D3DXQUATERNION((m._13 + m._31) / s, (m._23 + m._32) / s, 0.25f * s, (m._12 - m._21) / s);
    }
    *pOut = q;
    return pOut;
}

D3DXQUATERNION* WINAPI D3DXQuaternionSlerp(D3DXQUATERNION* pOut, CONST D3DXQUATERNION* pQ1, CONST D3DXQUATERNION* pQ2, FLOAT t)
{
    const D3DXQUATERNION a = *pQ1, b = *pQ2;
    float dot = DotQ(a, b), sign = 1.0f;
    if (dot < 0.0f) { sign = -1.0f; dot = -dot; }   // shortest arc
    float wa = 1.0f - t, wb = t;
    if (1.0f - dot > 0.001f) {
        const float theta = std::acos(dot), s = std::sin(theta);
        wa = std::sin(theta * (1.0f - t)) / s;
        wb = std::sin(theta * t) / s;
    }
    wb *= sign;
    *pOut = D3DXQUATERNION(wa * a.x + wb * b.x, wa * a.y + wb * b.y, wa * a.z + wb * b.z, wa * a.w + wb * b.w);
    return pOut;
}

D3DXQUATERNION* WINAPI D3DXQuaternionSquad(D3DXQUATERNION* pOut, CONST D3DXQUATERNION* pQ1, CONST D3DXQUATERNION* pA,
                                           CONST D3DXQUATERNION* pB, CONST D3DXQUATERNION* pC, FLOAT t)
{
    D3DXQUATERNION q1c, ab;
    D3DXQuaternionSlerp(&q1c, pQ1, pC, t);
    D3DXQuaternionSlerp(&ab, pA, pB, t);
    return D3DXQuaternionSlerp(pOut, &q1c, &ab, 2.0f * t * (1.0f - t));
}

// Control points for Squad between Q1 and Q2 (Q0/Q3 are the neighbours), sign-aligned to Q1.
void WINAPI D3DXQuaternionSquadSetup(D3DXQUATERNION* pAOut, D3DXQUATERNION* pBOut, D3DXQUATERNION* pCOut,
                                     CONST D3DXQUATERNION* pQ0, CONST D3DXQUATERNION* pQ1,
                                     CONST D3DXQUATERNION* pQ2, CONST D3DXQUATERNION* pQ3)
{
    const D3DXQUATERNION q1 = *pQ1;
    const D3DXQUATERNION q0 = DotQ(*pQ0, q1) < 0.0f ? -*pQ0 : *pQ0;
    const D3DXQUATERNION q2 = DotQ(q1, *pQ2) < 0.0f ? -*pQ2 : *pQ2;
    const D3DXQUATERNION q3 = DotQ(q2, *pQ3) < 0.0f ? -*pQ3 : *pQ3;
    // a = q1 * exp(-(ln(q1^-1 q2) + ln(q1^-1 q0)) / 4), in Hamilton order; D3DX's multiply
    // takes its arguments reversed, hence Multiply(x, y) == y*x below.
    auto ham = [](const D3DXQUATERNION& l, const D3DXQUATERNION& r) { D3DXQUATERNION o; D3DXQuaternionMultiply(&o, &r, &l); return o; };
    D3DXQUATERNION inv1, inv2;
    D3DXQuaternionInverse(&inv1, &q1);
    D3DXQuaternionInverse(&inv2, &q2);
    D3DXQUATERNION l1 = QLn(ham(inv1, q2)), l0 = QLn(ham(inv1, q0));
    const D3DXQUATERNION ea = QExp(D3DXQUATERNION(-0.25f * (l1.x + l0.x), -0.25f * (l1.y + l0.y), -0.25f * (l1.z + l0.z), 0.0f));
    D3DXQUATERNION l3 = QLn(ham(inv2, q3)), l2 = QLn(ham(inv2, q1));
    const D3DXQUATERNION eb = QExp(D3DXQUATERNION(-0.25f * (l3.x + l2.x), -0.25f * (l3.y + l2.y), -0.25f * (l3.z + l2.z), 0.0f));
    *pAOut = ham(q1, ea);
    *pBOut = ham(q2, eb);
    *pCOut = q2;
}

} // extern "C"
