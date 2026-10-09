// Small helper to build raylib meshes procedurally (non-indexed triangles, or indexed grids).
#pragma once

#include "raylib.h"
#include "raymath.h"

#include <cstring>
#include <vector>

namespace sky {

struct MeshBuilder {
    std::vector<float> pos, nrm, uv;
    std::vector<unsigned char> col;
    std::vector<unsigned short> idx;
    bool hasUv = false;

    size_t vertexCount() const { return pos.size() / 3; }

    void vertex(Vector3 p, Vector3 n, Color c, Vector2 t = {0, 0}) {
        pos.insert(pos.end(), {p.x, p.y, p.z});
        nrm.insert(nrm.end(), {n.x, n.y, n.z});
        col.insert(col.end(), {c.r, c.g, c.b, c.a});
        uv.insert(uv.end(), {t.x, t.y});
    }
    /// Flat-shaded triangle (counter-clockwise when seen from the front).
    void tri(Vector3 a, Vector3 b, Vector3 c, Color col0) {
        Vector3 n = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a)));
        vertex(a, n, col0); vertex(b, n, col0); vertex(c, n, col0);
    }
    void triN(Vector3 a, Vector3 b, Vector3 c, Vector3 na, Vector3 nb, Vector3 nc, Color col0) {
        vertex(a, na, col0); vertex(b, nb, col0); vertex(c, nc, col0);
    }
    void quad(Vector3 a, Vector3 b, Vector3 c, Vector3 d, Color col0) {  // a-b-c-d CCW
        tri(a, b, c, col0);
        tri(a, c, d, col0);
    }
    /// Double-sided triangle (for thin surfaces like wings without thickness).
    void tri2(Vector3 a, Vector3 b, Vector3 c, Color col0) { tri(a, b, c, col0); tri(a, c, b, col0); }

    void box(Vector3 center, Vector3 size, Color c, Matrix xf = MatrixIdentity()) {
        Vector3 h = Vector3Scale(size, 0.5f);
        Vector3 p[8];
        for (int i = 0; i < 8; ++i) {
            Vector3 v = {center.x + ((i & 1) ? h.x : -h.x), center.y + ((i & 2) ? h.y : -h.y), center.z + ((i & 4) ? h.z : -h.z)};
            p[i] = Vector3Transform(v, xf);
        }
        quad(p[0], p[4], p[6], p[2], c);  // -x
        quad(p[1], p[3], p[7], p[5], c);  // +x
        quad(p[0], p[1], p[5], p[4], c);  // -y
        quad(p[2], p[6], p[7], p[3], c);  // +y
        quad(p[0], p[2], p[3], p[1], c);  // -z
        quad(p[4], p[5], p[7], p[6], c);  // +z
    }
    /// Cylinder along +Y (or transformed).
    void cylinder(Vector3 base, float radius, float height, int sides, Color c, Matrix xf = MatrixIdentity(), float topRadius = -1) {
        if (topRadius < 0) topRadius = radius;
        for (int i = 0; i < sides; ++i) {
            float a0 = 2 * PI * i / sides, a1 = 2 * PI * (i + 1) / sides;
            Vector3 b0 = {base.x + cosf(a0) * radius, base.y, base.z + sinf(a0) * radius};
            Vector3 b1 = {base.x + cosf(a1) * radius, base.y, base.z + sinf(a1) * radius};
            Vector3 t0 = {base.x + cosf(a0) * topRadius, base.y + height, base.z + sinf(a0) * topRadius};
            Vector3 t1 = {base.x + cosf(a1) * topRadius, base.y + height, base.z + sinf(a1) * topRadius};
            Vector3 tc = {base.x, base.y + height, base.z};
            b0 = Vector3Transform(b0, xf); b1 = Vector3Transform(b1, xf);
            t0 = Vector3Transform(t0, xf); t1 = Vector3Transform(t1, xf); tc = Vector3Transform(tc, xf);
            quad(b0, t0, t1, b1, c);
            if (topRadius > 0) tri(tc, t1, t0, c);
        }
    }

    Mesh build(bool keepIndices = false) const {
        Mesh m = {0};
        m.vertexCount = static_cast<int>(vertexCount());
        m.triangleCount = keepIndices && !idx.empty() ? static_cast<int>(idx.size() / 3) : m.vertexCount / 3;
        m.vertices = static_cast<float*>(MemAlloc(sizeof(float) * pos.size()));
        std::memcpy(m.vertices, pos.data(), sizeof(float) * pos.size());
        m.normals = static_cast<float*>(MemAlloc(sizeof(float) * nrm.size()));
        std::memcpy(m.normals, nrm.data(), sizeof(float) * nrm.size());
        m.colors = static_cast<unsigned char*>(MemAlloc(col.size()));
        std::memcpy(m.colors, col.data(), col.size());
        m.texcoords = static_cast<float*>(MemAlloc(sizeof(float) * uv.size()));
        std::memcpy(m.texcoords, uv.data(), sizeof(float) * uv.size());
        if (keepIndices && !idx.empty()) {
            m.indices = static_cast<unsigned short*>(MemAlloc(sizeof(unsigned short) * idx.size()));
            std::memcpy(m.indices, idx.data(), sizeof(unsigned short) * idx.size());
        }
        UploadMesh(&m, false);
        return m;
    }
};

inline Vector3 V3(double x, double y, double z) { return {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)}; }

}  // namespace sky
