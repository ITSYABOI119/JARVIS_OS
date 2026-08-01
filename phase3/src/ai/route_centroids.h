/*
 * JARVIS AI-OS - Phase C: the FROZEN routing centroids (C/M4)
 *
 * GENERATED FILE - DO NOT EDIT BY HAND.
 *   generator:  phase3/scripts/embed/gen_route_centroids.py
 *   drift gate: `gen_route_centroids.py --check` (CI)
 *   verified against the npz bit-for-bit by
 *               phase3/scripts/embed/test_route_centroids.py (CI)
 *
 * WHAT THESE ARE
 *   Three unit vectors, one per route_class_t, living in the SAME 128-dimension
 *   mean-projected space g3_select_semantic compares in. A query embedded through
 *   that pipeline is classified by NEAREST CENTROID; cosine == dot because both
 *   sides are L2-normalised.
 *
 *   These are DATA, not a decision. Nothing here routes anything: whether a
 *   centroid may override, veto or merely inform route_classify is a separate
 *   question with its own measurement (cm4_routing_results.json).
 *
 * PROVENANCE
 *   source       phase3/src/ai/routing_suite.h :: ROUTING_DEV
 *   suite sha256 acd13ee5438ac7a1707fd453b6ba2ad6592398ffd3f4f8f4b44737f7b00d90f2
 *                (LF-normalised bytes, so autocrlf does not change it)
 *   npz          phase3/scripts/embed/route_centroids.npz :: centroids.npy
 *   npz sha256   d6b825d9c0bc3935055940b9f543adebdfce13e1beb28b771daf66e97ef828d6
 *   model        Qwen/Qwen3-Embedding-0.6B -- golden_meta.json carries the full
 *                identity (repo/file/quant/sha); never quote it from memory
 *   pipeline     embed(1024, sym:none, no prefix, normalize_embeddings=True,
 *                batch_size=16) -> mean-project with golden_vectors.npz's frozen
 *                mu -> truncate 128 -> L2 renormalise.  The generator IMPORTS
 *                cm2_floor.prepare rather than reimplementing it, so this cannot
 *                drift from the configuration the 0.55 floor was measured in.
 *                All of it in float64; the float32 cast happens once, at storage.
 *   members      ROUTE_INFER n=24, ROUTE_SYSFACTS n=30, ROUTE_DECLINE n=10  (64 DEV cases)
 *   ||c||^2      row0 0.999999986404, row1 0.999999994983, row2 1.000000012561
 *                (double-precision sum over the STORED float32 values)
 *
 * ==================== DEV SPLIT ONLY. NEVER HELDOUT. ====================
 *
 * Built from ROUTING_DEV and only ROUTING_DEV. ROUTING_HELDOUT is not read by the
 * generator at all -- not to fit, not to tune, not to sanity-check. That is
 * routing_suite.h's own rule, and it is the entire reason the held-out number
 * means anything: a centroid fitted on HELDOUT would make it self-referential.
 *
 * ROW ORDER IS route_class_t
 *   [0] ROUTE_INFER   [1] ROUTE_SYSFACTS   [2] ROUTE_DECLINE
 * Compile-enforced by the _Static_asserts below, not merely asserted here -- a
 * silently permuted row set is a plausible-looking artifact that misroutes every
 * query, and no runtime check would catch it.
 *
 * ==================== FROZEN ====================
 *
 * Regenerating these vectors -- from a different embedder, a different DEV split,
 * a different pooling configuration, or the same one on a different GPU -- makes
 * every number measured against them stale, including the C/M4 arm results and
 * any probe-leg expectation derived from them. They are not tuned constants; they
 * are a measurement. A new corpus means a new measurement, not a new header.
 *
 * mu-COUPLING: these vectors live in the space `mu` defines. embed_mu.h and this
 * header are a MATCHED PAIR. A Matryoshka re-truncation, a new mu, or any change
 * to the projection must regenerate BOTH artifacts together -- regenerating one
 * alone leaves the box comparing vectors from two different spaces, which produces
 * confident, well-formed, wrong answers rather than an error.
 *
 * DIMENSION: 128, matching g3_retrieval.h's G3_SEM_DIM_DEFAULT -- the space AFTER
 * Matryoshka truncation. NOT embed_region.h's EMBED_DIM of 1024, which is where
 * mu is applied. Both constants are correct and distinct; embed_store.h:138
 * documents why they must not be reconciled to one value.
 *
 * RENORMALISE-AT-USE inherits embed_mu.h's guidance unchanged: the float32 cast
 * leaves ||c|| a hair off 1.0 (see the values above), so a consumer that needs a
 * true cosine should normalise, and should record which precision it accumulated
 * the norm in. The effect is ~1e-7 on any dot product -- immaterial to the
 * operating point, documented so a later session does not read it as a port bug.
 */

#ifndef ROUTE_CENTROIDS_H
#define ROUTE_CENTROIDS_H

#include <stdint.h>
#include "route.h"          /* route_class_t - row order is compile-enforced */
#include "g3_retrieval.h"   /* G3_SEM_DIM_DEFAULT - the measured 128-d space */

#define ROUTE_CENTROIDS_N    3
#define ROUTE_CENTROIDS_DIM  128

/* XOR of the INTENDED binary32 bit patterns, row-major, computed by the generator
 * from the npz. A C consumer can recompute it from the compiled array, which is
 * what turns 'hex literals are exact' from an assumption into a verified property;
 * test_route_centroids.py checks this constant against the npz AND against the
 * header's own literals, so any such C check chains back to the npz rather than to
 * something derived from this file. */
#define ROUTE_CENTROIDS_XOR  0x0964C995u

static const float ROUTE_CENTROIDS[ROUTE_CENTROIDS_N][ROUTE_CENTROIDS_DIM] = {
    /* [0] ROUTE_INFER -- mean of 24 DEV cases */
    {
        0x1.6066fep-5f, -0x1.983cdcp-3f, -0x1.16ac6ap-8f, -0x1.a5798ap-3f,
        -0x1.c39ef4p-5f, 0x1.1981a2p-3f, -0x1.502adcp-4f, -0x1.4e2aa8p-6f,
        -0x1.15b2dep-5f, -0x1.97968p-6f, 0x1.11e894p-4f, -0x1.328004p-3f,
        0x1.bcbfd4p-4f, -0x1.38caep-8f, 0x1.63726cp-6f, -0x1.d72b02p-6f,
        -0x1.28bddep-5f, -0x1.f4d904p-4f, -0x1.3a7b5ap-2f, -0x1.ca884p-4f,
        0x1.9c0ffep-5f, 0x1.0d6e7ep-3f, -0x1.85e736p-4f, 0x1.6b7118p-4f,
        0x1.540aacp-7f, -0x1.375834p-4f, -0x1.524e52p-4f, -0x1.ec1b82p-5f,
        -0x1.25a11ap-3f, -0x1.e533b8p-4f, -0x1.e129a6p-4f, -0x1.3b9e46p-4f,
        -0x1.0eb89p-4f, -0x1.0be382p-4f, 0x1.ad3034p-7f, -0x1.601a76p-10f,
        -0x1.3b82c2p-6f, -0x1.e1fbdep-4f, 0x1.83976cp-4f, -0x1.ed655p-5f,
        0x1.2af7c8p-4f, -0x1.741612p-4f, 0x1.369bcp-7f, -0x1.00733p-4f,
        0x1.07f7acp-4f, 0x1.15fe0ep-11f, 0x1.273bc6p-3f, 0x1.c7b902p-4f,
        0x1.739936p-3f, 0x1.1b24d2p-6f, 0x1.6c6abcp-5f, -0x1.2082ecp-5f,
        -0x1.518476p-6f, 0x1.54a52cp-7f, -0x1.95942p-6f, 0x1.127cdcp-5f,
        0x1.7a00a8p-6f, 0x1.5f1efap-4f, -0x1.048766p-5f, -0x1.62acp-5f,
        0x1.61326cp-5f, -0x1.26fb28p-4f, -0x1.3ba664p-3f, 0x1.3f26fcp-4f,
        -0x1.a5c62ep-6f, 0x1.af2f06p-3f, -0x1.62629cp-4f, -0x1.3e85cp-3f,
        0x1.8f4664p-4f, 0x1.eebaf6p-6f, -0x1.a880ecp-4f, -0x1.92451ap-3f,
        0x1.2481e8p-5f, 0x1.d82e5cp-9f, -0x1.f63eb6p-5f, 0x1.dce254p-6f,
        -0x1.cd9c54p-4f, -0x1.2b161cp-12f, 0x1.3cebdp-4f, 0x1.9d0d3ap-6f,
        0x1.5d064ap-5f, 0x1.dbcbb8p-5f, 0x1.4a572ep-7f, 0x1.2a0932p-5f,
        -0x1.924848p-5f, 0x1.a43246p-5f, -0x1.c3685p-5f, 0x1.a0b2b4p-4f,
        0x1.4de516p-6f, -0x1.87220cp-5f, 0x1.4832eep-4f, 0x1.ae7996p-4f,
        0x1.572004p-4f, -0x1.00bc58p-3f, 0x1.d2bde2p-5f, -0x1.7830b6p-6f,
        -0x1.a5820cp-5f, -0x1.3948bcp-9f, -0x1.f72e36p-6f, 0x1.1f302cp-18f,
        0x1.d92cfap-5f, 0x1.9645dp-5f, -0x1.53274cp-3f, 0x1.12aeb2p-3f,
        -0x1.77b8e4p-4f, -0x1.0c9988p-6f, 0x1.41b5c2p-3f, -0x1.8d2954p-4f,
        -0x1.4566a2p-5f, -0x1.e69618p-6f, 0x1.7608p-8f, 0x1.d37fcap-6f,
        -0x1.cd5476p-3f, 0x1.23ed82p-4f, -0x1.ae14c8p-6f, 0x1.30e546p-6f,
        -0x1.2bb8a6p-3f, 0x1.6d7d6ep-5f, 0x1.5a9a76p-8f, 0x1.7a8db4p-5f,
        -0x1.b9c4bcp-5f, -0x1.6f2a1ap-6f, 0x1.9dfa22p-4f, -0x1.a244fcp-6f,
        -0x1.6499dap-6f, -0x1.758988p-6f, -0x1.9bd7b8p-6f, 0x1.29df5p-4f,
    },
    /* [1] ROUTE_SYSFACTS -- mean of 30 DEV cases */
    {
        0x1.5e46bap-4f, 0x1.336bbap-7f, -0x1.27fa96p-9f, -0x1.bdcb9p-4f,
        0x1.fee04cp-5f, -0x1.2e139cp-14f, 0x1.1a85aep-3f, 0x1.b8ed34p-4f,
        -0x1.011b94p-6f, -0x1.5e80ap-4f, -0x1.048a1ap-3f, 0x1.14f47cp-4f,
        0x1.3ca8dcp-3f, -0x1.6f53cap-8f, 0x1.5725dcp-6f, -0x1.2b143cp-4f,
        -0x1.76059ep-4f, -0x1.16acb6p-4f, 0x1.e489e4p-5f, 0x1.a12d26p-7f,
        0x1.8e9344p-5f, 0x1.ae513ap-4f, -0x1.f1fd3cp-4f, -0x1.e821cp-6f,
        0x1.ddf682p-6f, 0x1.339a04p-7f, 0x1.46d2cep-6f, 0x1.5a025p-3f,
        -0x1.198b4p-3f, 0x1.920318p-4f, 0x1.0bd7aep-4f, 0x1.50201ep-5f,
        -0x1.f4267ep-9f, -0x1.49a036p-4f, -0x1.850ad4p-6f, -0x1.2933a6p-8f,
        0x1.9e2eb2p-6f, 0x1.d7f0e6p-4f, -0x1.7a5fecp-5f, 0x1.641bap-6f,
        -0x1.30bc06p-3f, 0x1.2d4ed2p-3f, 0x1.33ea66p-4f, 0x1.cda818p-6f,
        -0x1.201b5ep-3f, 0x1.b41738p-5f, 0x1.1534ecp-4f, 0x1.66a4f4p-6f,
        -0x1.7e8232p-3f, -0x1.b065a4p-6f, -0x1.5095eap-5f, -0x1.cae52cp-4f,
        0x1.818d7p-5f, 0x1.8f2306p-5f, 0x1.18429p-4f, 0x1.2f044p-4f,
        0x1.04b5f6p-3f, -0x1.ee54d6p-5f, -0x1.4c0cecp-4f, 0x1.a48818p-5f,
        -0x1.45db4cp-2f, -0x1.dd393ap-9f, 0x1.92f5cp-8f, -0x1.050bc8p-4f,
        0x1.ec944cp-5f, 0x1.e66fa2p-4f, -0x1.0c112cp-4f, -0x1.b14048p-4f,
        0x1.96d068p-4f, -0x1.a76f9ap-5f, 0x1.da249cp-8f, 0x1.0eaa6ap-5f,
        -0x1.10c1d6p-4f, 0x1.a0ac12p-5f, -0x1.556c8p-5f, -0x1.8779bp-4f,
        -0x1.2fc324p-5f, 0x1.6ae6d8p-8f, -0x1.5a392ap-5f, -0x1.235176p-3f,
        0x1.62250cp-4f, -0x1.920ca4p-3f, -0x1.ea2cd8p-5f, 0x1.bf0e8p-5f,
        0x1.324d5p-4f, -0x1.5077a2p-3f, 0x1.1247fep-3f, -0x1.2d4f54p-5f,
        -0x1.51df8cp-5f, 0x1.57f50ep-4f, 0x1.99e3cep-7f, -0x1.5806d6p-4f,
        0x1.29df72p-4f, -0x1.1ee6cap-3f, -0x1.2ed666p-9f, 0x1.cb1de4p-8f,
        -0x1.1aec64p-6f, 0x1.1ff9f4p-4f, -0x1.1a6934p-7f, -0x1.7c90ap-6f,
        0x1.4b7e74p-5f, -0x1.ca8bc8p-5f, 0x1.b98166p-5f, -0x1.1dce38p-6f,
        0x1.a3bd44p-7f, 0x1.ee91c8p-6f, 0x1.dc2a5ep-6f, -0x1.d6354ap-4f,
        -0x1.c26674p-6f, -0x1.99ceep-4f, -0x1.61930cp-5f, -0x1.8b6018p-4f,
        0x1.b97ad2p-4f, 0x1.7ea706p-3f, -0x1.cf3d52p-5f, -0x1.4b4c34p-6f,
        -0x1.62df8p-2f, -0x1.80e198p-6f, -0x1.4cb682p-7f, -0x1.80861ep-6f,
        0x1.6433c2p-4f, 0x1.3003d2p-5f, -0x1.aca182p-5f, 0x1.a7fdb6p-5f,
        -0x1.2d852p-6f, 0x1.659956p-7f, 0x1.189134p-4f, -0x1.8397cep-5f,
    },
    /* [2] ROUTE_DECLINE -- mean of 10 DEV cases */
    {
        -0x1.9b2e86p-4f, -0x1.dc0c86p-5f, 0x1.91ae6cp-8f, -0x1.d2b924p-4f,
        0x1.490a7ep-4f, 0x1.2287ap-4f, 0x1.4741b6p-4f, 0x1.015972p-4f,
        -0x1.50c474p-6f, -0x1.270dbap-3f, 0x1.264cbap-4f, 0x1.6211c6p-3f,
        0x1.aff7fep-3f, 0x1.30c0d4p-8f, 0x1.0f5ee8p-4f, -0x1.130ab2p-3f,
        0x1.2f2b1ep-4f, -0x1.41579ep-4f, 0x1.0a2f26p-3f, 0x1.500e1ep-4f,
        0x1.b21f82p-6f, 0x1.c2c1f2p-4f, -0x1.54f56p-3f, -0x1.3d91bep-4f,
        -0x1.0a9942p-9f, 0x1.c2f3c2p-7f, -0x1.744338p-7f, 0x1.e0ebccp-5f,
        -0x1.101992p-3f, 0x1.d4e004p-11f, 0x1.d2ea1ap-4f, -0x1.1cbc9p-10f,
        -0x1.7b0e88p-5f, -0x1.2086a4p-5f, -0x1.069088p-7f, 0x1.f3a756p-8f,
        0x1.a71d56p-4f, 0x1.43c8c4p-3f, 0x1.7cabdep-5f, -0x1.e3f7bp-6f,
        -0x1.047834p-6f, 0x1.ebc234p-4f, 0x1.2563e2p-3f, -0x1.5c41c8p-8f,
        -0x1.51be5ap-4f, -0x1.af5b38p-6f, 0x1.0a53b8p-5f, 0x1.593b2p-6f,
        -0x1.5b471cp-3f, -0x1.fe1eb6p-5f, -0x1.458fp-9f, -0x1.943472p-6f,
        -0x1.41c81p-5f, -0x1.09bc26p-5f, -0x1.44e05ep-6f, 0x1.67cf7ep-4f,
        0x1.bac60ep-4f, 0x1.8f0314p-6f, 0x1.f75fb4p-11f, 0x1.1cfd94p-4f,
        -0x1.3efbaep-2f, 0x1.36794ap-4f, -0x1.0bb346p-7f, -0x1.f2b082p-6f,
        0x1.6adabcp-5f, 0x1.8ff0f6p-4f, -0x1.7048cep-4f, -0x1.d0c144p-4f,
        -0x1.002f92p-3f, -0x1.7f9a14p-4f, -0x1.752d44p-8f, -0x1.0c5f44p-4f,
        -0x1.6d0fp-5f, 0x1.c24642p-7f, -0x1.d0ee0cp-5f, 0x1.d96e0cp-5f,
        -0x1.6aa684p-4f, 0x1.818844p-5f, -0x1.7fff7cp-6f, -0x1.796788p-4f,
        0x1.14d50cp-4f, -0x1.7350aap-4f, 0x1.234114p-5f, -0x1.9cef6ap-6f,
        -0x1.7fc5a4p-5f, -0x1.e56a28p-3f, 0x1.5b30acp-4f, -0x1.93877p-7f,
        -0x1.84d924p-4f, 0x1.3df368p-6f, 0x1.38ef78p-4f, -0x1.29f42p-3f,
        -0x1.5ee018p-4f, -0x1.2facc4p-3f, -0x1.3aa596p-3f, 0x1.ff2f02p-6f,
        -0x1.e5bc12p-6f, -0x1.797976p-5f, -0x1.9e7a34p-7f, 0x1.135bcep-6f,
        0x1.3b55cep-5f, -0x1.66e3e8p-7f, 0x1.5ecc24p-4f, 0x1.d02ad6p-5f,
        -0x1.be1cbap-4f, -0x1.6104f6p-7f, 0x1.2187f2p-5f, -0x1.16507ap-4f,
        0x1.c0c246p-5f, -0x1.01dd1ep-6f, 0x1.881648p-8f, -0x1.9e0c7p-5f,
        -0x1.8226fcp-5f, 0x1.2f143cp-4f, -0x1.5a88ecp-3f, 0x1.290edp-5f,
        -0x1.49ce3p-2f, 0x1.01ad0cp-5f, -0x1.d8249ep-6f, 0x1.01d76cp-5f,
        0x1.8f8b02p-4f, -0x1.17856ap-7f, 0x1.d272f4p-5f, 0x1.5457a8p-8f,
        -0x1.256d3ap-6f, 0x1.330de2p-6f, 0x1.030ac4p-4f, -0x1.38b1acp-6f,
    },
};

_Static_assert(sizeof(ROUTE_CENTROIDS) / sizeof(ROUTE_CENTROIDS[0]) == ROUTE_CENTROIDS_N,
               "centroid row count must be ROUTE_CENTROIDS_N");
_Static_assert(sizeof(ROUTE_CENTROIDS[0]) / sizeof(float) == ROUTE_CENTROIDS_DIM,
               "each centroid must be ROUTE_CENTROIDS_DIM floats");
/* The rows ARE route_class_t values. If a fourth class is ever appended, this
 * breaks the build instead of silently leaving the new class with no centroid
 * and every query for it falling to whichever row happens to score highest. */
_Static_assert(ROUTE_CENTROIDS_N == (int)ROUTE_DECLINE + 1,
               "a new route_class_t needs a new centroid row");
_Static_assert((int)ROUTE_INFER == 0 && (int)ROUTE_SYSFACTS == 1 && (int)ROUTE_DECLINE == 2,
               "centroid row order is route_class_t order");
/* The centroids are compared against vectors produced by the C/M2-measured
 * pipeline, so a change to either dimension must break this build rather than
 * silently compare across two different spaces. */
_Static_assert(ROUTE_CENTROIDS_DIM == G3_SEM_DIM_DEFAULT,
               "centroid dimension must equal the measured semantic space");

#endif /* ROUTE_CENTROIDS_H */
