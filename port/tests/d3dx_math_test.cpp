// Behaviour tests for port/d3dx/d3dx9_math.cpp. Every function is checked against an
// independent derivation (matrix vs quaternion forms, inverse * M = I, known projections...),
// so a wrong sign or order in any one formula shows up.
#include "ran_compat.h"
#include <d3dx9math.h>
#include <cmath>
#include <cstdio>

static int g_failed = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failed; } } while (0)

static bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }
static bool NearM(const D3DXMATRIX& a, const D3DXMATRIX& b, float eps = 1e-4f)
{
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) if (!Near(a.m[i][j], b.m[i][j], eps)) return false;
    return true;
}
static bool NearV(const D3DXVECTOR3& a, const D3DXVECTOR3& b, float eps = 1e-4f) { return Near(a.x, b.x, eps) && Near(a.y, b.y, eps) && Near(a.z, b.z, eps); }
static bool NearQ(const D3DXQUATERNION& a, const D3DXQUATERNION& b, float eps = 1e-4f)   // q and -q are the same rotation
{
    const bool same = Near(a.x, b.x, eps) && Near(a.y, b.y, eps) && Near(a.z, b.z, eps) && Near(a.w, b.w, eps);
    const bool neg = Near(a.x, -b.x, eps) && Near(a.y, -b.y, eps) && Near(a.z, -b.z, eps) && Near(a.w, -b.w, eps);
    return same || neg;
}
static D3DXMATRIX Ident() { D3DXMATRIX m; for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) m.m[i][j] = i == j; return m; }

int main()
{
    const float yaws[] = { 0.0f, 0.3f, -1.2f, 2.5f }, pitches[] = { 0.0f, 0.7f, -0.4f, 1.4f }, rolls[] = { 0.0f, -0.9f, 0.2f, 3.0f };

    // Axis rotations agree with the generic axis/quaternion forms.
    for (float a : { 0.0f, 0.5f, -1.3f, 3.1f }) {
        D3DXMATRIX mx, my, mz, ma, mq;
        D3DXQUATERNION q;
        const D3DXVECTOR3 X(1, 0, 0), Y(0, 1, 0), Z(0, 0, 1), V(1, 2, -3);
        D3DXMatrixRotationX(&mx, a); D3DXMatrixRotationAxis(&ma, &X, a); CHECK(NearM(mx, ma));
        D3DXMatrixRotationY(&my, a); D3DXMatrixRotationAxis(&ma, &Y, a); CHECK(NearM(my, ma));
        D3DXMatrixRotationZ(&mz, a); D3DXMatrixRotationAxis(&ma, &Z, a); CHECK(NearM(mz, ma));
        D3DXMatrixRotationAxis(&ma, &V, a);
        D3DXQuaternionRotationAxis(&q, &V, a);
        D3DXMatrixRotationQuaternion(&mq, &q);
        CHECK(NearM(ma, mq));
    }
    // RotationX(90deg) maps +Y to +Z (left-handed, row vectors).
    {
        D3DXMATRIX m; D3DXVECTOR3 v(0, 1, 0), o;
        D3DXMatrixRotationX(&m, D3DX_PI / 2);
        D3DXVec3TransformCoord(&o, &v, &m);
        CHECK(NearV(o, D3DXVECTOR3(0, 0, 1)));
    }
    // Yaw/pitch/roll: quaternion form == matrix form == Z*X*Y composition.
    for (float y : yaws) for (float p : pitches) for (float r : rolls) {
        D3DXMATRIX m1, m2, mz, mx, my, m3;
        D3DXQUATERNION q;
        D3DXMatrixRotationYawPitchRoll(&m1, y, p, r);
        D3DXQuaternionRotationYawPitchRoll(&q, y, p, r);
        D3DXMatrixRotationQuaternion(&m2, &q);
        CHECK(NearM(m1, m2));
        D3DXMatrixRotationZ(&mz, r); D3DXMatrixRotationX(&mx, p); D3DXMatrixRotationY(&my, y);
        D3DXMatrixMultiply(&m3, &mz, &mx); D3DXMatrixMultiply(&m3, &m3, &my);
        CHECK(NearM(m1, m3));
        // Matrix -> quaternion -> matrix round trip (exercises every RotationMatrix branch).
        D3DXQUATERNION back; D3DXMATRIX m4;
        D3DXQuaternionRotationMatrix(&back, &m1);
        D3DXMatrixRotationQuaternion(&m4, &back);
        CHECK(NearM(m1, m4));
        CHECK(NearQ(back, q));
    }
    // 180-degree rotations hit the non-trace branches of RotationMatrix.
    for (const D3DXVECTOR3& axis : { D3DXVECTOR3(1, 0, 0), D3DXVECTOR3(0, 1, 0), D3DXVECTOR3(0, 0, 1) }) {
        D3DXMATRIX m; D3DXQUATERNION q, e;
        D3DXMatrixRotationAxis(&m, &axis, D3DX_PI);
        D3DXQuaternionRotationMatrix(&q, &m);
        D3DXQuaternionRotationAxis(&e, &axis, D3DX_PI);
        CHECK(NearQ(q, e));
    }
    // Multiply order: Multiply(q1, q2) rotates by q1 then q2 == M(q1) * M(q2).
    {
        D3DXQUATERNION q1, q2, q12; D3DXMATRIX m1, m2, m12, mq;
        D3DXQuaternionRotationYawPitchRoll(&q1, 0.4f, -0.2f, 1.1f);
        D3DXQuaternionRotationYawPitchRoll(&q2, -1.0f, 0.6f, 0.3f);
        D3DXQuaternionMultiply(&q12, &q1, &q2);
        D3DXMatrixRotationQuaternion(&m1, &q1); D3DXMatrixRotationQuaternion(&m2, &q2);
        D3DXMatrixMultiply(&m12, &m1, &m2);
        D3DXMatrixRotationQuaternion(&mq, &q12);
        CHECK(NearM(m12, mq));
        // Inverse undoes it; Normalize of a scaled quaternion is unit length.
        D3DXQUATERNION inv, id; D3DXQuaternionInverse(&inv, &q1); D3DXQuaternionMultiply(&id, &q1, &inv);
        CHECK(NearQ(id, D3DXQUATERNION(0, 0, 0, 1)));
        D3DXQUATERNION big(2, 4, 4, 0), n; D3DXQuaternionNormalize(&n, &big);
        CHECK(NearQ(n, D3DXQUATERNION(1.0f / 3, 2.0f / 3, 2.0f / 3, 0)));
    }
    // Inverse / determinant / transpose / translation / scaling.
    {
        D3DXMATRIX r, t, s, m, inv, prod; float det = 0;
        D3DXMatrixRotationYawPitchRoll(&r, 0.3f, 1.0f, -0.5f);
        D3DXMatrixTranslation(&t, 5, -2, 7);
        D3DXMatrixScaling(&s, 2, 3, 4);
        D3DXMatrixMultiply(&m, &s, &r); D3DXMatrixMultiply(&m, &m, &t);
        CHECK(D3DXMatrixInverse(&inv, &det, &m) == &inv);
        CHECK(Near(det, 24.0f, 1e-3f));
        D3DXMatrixMultiply(&prod, &m, &inv);
        CHECK(NearM(prod, Ident()));
        D3DXMatrixInverse(&m, nullptr, &m);   // aliasing
        CHECK(NearM(m, inv));
        D3DXMATRIX z = Ident(); z._22 = 0;
        CHECK(D3DXMatrixInverse(&inv, &det, &z) == nullptr && det == 0.0f);
        D3DXMATRIX tt; D3DXMatrixTranspose(&tt, &r); D3DXMatrixMultiply(&prod, &r, &tt);
        CHECK(NearM(prod, Ident()));   // rotation^T = rotation^-1
        D3DXVECTOR3 p(1, 1, 1), o;
        D3DXVec3TransformCoord(&o, &p, &t); CHECK(NearV(o, D3DXVECTOR3(6, -1, 8)));
        D3DXVec3TransformNormal(&o, &p, &t); CHECK(NearV(o, p));   // normals ignore translation
        D3DXVec3TransformNormal(&o, &p, &s); CHECK(NearV(o, D3DXVECTOR3(2, 3, 4)));
        D3DXVECTOR4 v4(1, 2, 3, 1), o4; D3DXVec4Transform(&o4, &v4, &t);
        CHECK(Near(o4.x, 6) && Near(o4.y, 0) && Near(o4.z, 10) && Near(o4.w, 1));
    }
    // View/projection matrices.
    {
        D3DXVECTOR3 eye(3, 4, -10), at(3, 4, 0), up(0, 1, 0), o;
        D3DXMATRIX view; D3DXMatrixLookAtLH(&view, &eye, &at, &up);
        D3DXVec3TransformCoord(&o, &eye, &view); CHECK(NearV(o, D3DXVECTOR3(0, 0, 0)));
        D3DXVec3TransformCoord(&o, &at, &view);  CHECK(NearV(o, D3DXVECTOR3(0, 0, 10)));
        D3DXVECTOR3 right(4, 4, -10); D3DXVec3TransformCoord(&o, &right, &view); CHECK(NearV(o, D3DXVECTOR3(1, 0, 0)));

        D3DXMATRIX proj; D3DXMatrixPerspectiveFovLH(&proj, D3DX_PI / 2, 2.0f, 1.0f, 100.0f);
        D3DXVECTOR3 nearPt(0, 0, 1), farPt(0, 0, 100), edge(2, 1, 1);
        D3DXVec3TransformCoord(&o, &nearPt, &proj); CHECK(Near(o.z, 0.0f));
        D3DXVec3TransformCoord(&o, &farPt, &proj);  CHECK(Near(o.z, 1.0f));
        D3DXVec3TransformCoord(&o, &edge, &proj);   CHECK(Near(o.x, 1.0f) && Near(o.y, 1.0f));   // 90deg fov, aspect 2

        D3DXMATRIX ortho; D3DXMatrixOrthoLH(&ortho, 800, 600, 1.0f, 11.0f);
        D3DXVECTOR3 a(400, -300, 1), b(0, 0, 11);
        D3DXVec3TransformCoord(&o, &a, &ortho); CHECK(NearV(o, D3DXVECTOR3(1, -1, 0)));
        D3DXVec3TransformCoord(&o, &b, &ortho); CHECK(Near(o.z, 1.0f));

        D3DVIEWPORT9 vp = { 10, 20, 800, 600, 0.0f, 1.0f };
        D3DXMATRIX world = Ident();
        D3DXVec3Project(&o, &at, &vp, &proj, &view, &world);
        CHECK(Near(o.x, 410.0f) && Near(o.y, 320.0f));   // straight ahead -> viewport centre
        D3DXVECTOR3 upPt(3, 9, 0); D3DXVec3Project(&o, &upPt, &vp, &proj, &view, nullptr);
        CHECK(o.y < 320.0f);   // above the axis -> towards the top of the screen
    }
    // Planes.
    {
        D3DXVECTOR3 p1(0, 0, 0), p2(1, 0, 0), p3(0, 1, 0), o;
        D3DXPLANE pl; D3DXPlaneFromPoints(&pl, &p1, &p2, &p3);
        CHECK(Near(pl.a, 0) && Near(pl.b, 0) && Near(pl.c, 1) && Near(pl.d, 0));
        D3DXVECTOR3 l1(2, 3, -1), l2(2, 3, 3);
        CHECK(D3DXPlaneIntersectLine(&o, &pl, &l1, &l2) == &o && NearV(o, D3DXVECTOR3(2, 3, 0)));
        D3DXVECTOR3 m1(0, 0, 1), m2(5, 5, 1);
        CHECK(D3DXPlaneIntersectLine(&o, &pl, &m1, &m2) == nullptr);   // parallel
        D3DXVECTOR3 pt(0, 0, 4), n(0, 0, 2);
        D3DXPlaneFromPointNormal(&pl, &pt, &n); CHECK(Near(pl.d, -8));
        // Move plane z=0 by +5 in z: transform with the inverse transpose.
        D3DXPLANE z0(0, 0, 1, 0), moved; D3DXMATRIX t, it;
        D3DXMatrixTranslation(&t, 0, 0, 5); D3DXMatrixInverse(&it, nullptr, &t); D3DXMatrixTranspose(&it, &it);
        D3DXPlaneTransform(&moved, &z0, &it);
        CHECK(Near(moved.c, 1) && Near(moved.d, -5));
    }
    // Interpolation.
    {
        const D3DXVECTOR3 Y(0, 1, 0);
        D3DXQUATERNION q[4], o;
        for (int i = 0; i < 4; ++i) D3DXQuaternionRotationAxis(&q[i], &Y, 0.4f * i);
        D3DXQuaternionSlerp(&o, &q[1], &q[2], 0.0f); CHECK(NearQ(o, q[1]));
        D3DXQuaternionSlerp(&o, &q[1], &q[2], 1.0f); CHECK(NearQ(o, q[2]));
        D3DXQUATERNION half; D3DXQuaternionRotationAxis(&half, &Y, 0.6f);
        D3DXQuaternionSlerp(&o, &q[1], &q[2], 0.5f); CHECK(NearQ(o, half));
        D3DXQUATERNION negq2 = -q[2];   // shortest arc regardless of sign
        D3DXQuaternionSlerp(&o, &q[1], &negq2, 0.5f); CHECK(NearQ(o, half));
        // Evenly spaced keys about one axis: the squad controls are the keys themselves.
        D3DXQUATERNION a, b, c;
        D3DXQuaternionSquadSetup(&a, &b, &c, &q[0], &q[1], &q[2], &q[3]);
        CHECK(NearQ(a, q[1]) && NearQ(b, q[2]) && NearQ(c, q[2]));
        D3DXQuaternionSquad(&o, &q[1], &a, &b, &c, 0.0f); CHECK(NearQ(o, q[1]));
        D3DXQuaternionSquad(&o, &q[1], &a, &b, &c, 1.0f); CHECK(NearQ(o, q[2]));
        D3DXQuaternionSquad(&o, &q[1], &a, &b, &c, 0.5f); CHECK(NearQ(o, half));

        D3DXVECTOR3 v0(0, 0, 0), v1(1, 0, 0), v2(2, 1, 0), v3(3, 1, 0), cr;
        D3DXVec3CatmullRom(&cr, &v0, &v1, &v2, &v3, 0.0f); CHECK(NearV(cr, v1));
        D3DXVec3CatmullRom(&cr, &v0, &v1, &v2, &v3, 1.0f); CHECK(NearV(cr, v2));
    }
    // Normalize.
    {
        D3DXVECTOR3 v(3, 0, 4), o; D3DXVec3Normalize(&o, &v); CHECK(NearV(o, D3DXVECTOR3(0.6f, 0, 0.8f)));
        D3DXVECTOR3 z(0, 0, 0); D3DXVec3Normalize(&o, &z); CHECK(NearV(o, z));
        D3DXVECTOR2 v2(0, -5), o2; D3DXVec2Normalize(&o2, &v2); CHECK(Near(o2.x, 0) && Near(o2.y, -1));
        D3DXVECTOR4 v4(1, 1, 1, 1), o4; D3DXVec4Normalize(&o4, &v4); CHECK(Near(o4.w, 0.5f));
    }

    if (g_failed) { std::printf("%d check(s) failed\n", g_failed); return 1; }
    std::printf("d3dx_math_test: all checks passed\n");
    return 0;
}
