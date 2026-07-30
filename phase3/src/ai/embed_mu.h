/*
 * JARVIS AI-OS - Phase C: the frozen mean-projection direction `mu`
 *
 * GENERATED FILE - DO NOT EDIT BY HAND.
 *   generator: phase3/scripts/embed/gen_embed_mu.py
 *   drift gate: `gen_embed_mu.py --check` (CI)
 *   verified against the npz bit-for-bit by phase3/scripts/embed/test_embed_mu.py (CI)
 *
 * PROVENANCE
 *   source      phase3/scripts/embed/golden_vectors.npz :: mu.npy
 *   npz sha256  01c7fe08eb4967c1d72870d37b75ca8c5054f44dbdcfa038e2098b34eb5f0ecf
 *   dtype/shape <f4 (1024,) = 4096 raw bytes
 *   model       Qwen/Qwen3-Embedding-0.6B (see golden_meta.json for the
 *               full identity - never quote model identity from memory)
 *   ||mu||^2    0.9999999746000995 (double-precision sum of the stored values)
 *
 * FROZEN. Regenerating `mu` from a different embedder, a different corpus, or
 * a different pooling configuration INVALIDATES cm2_floor_results.json and
 * therefore invalidates the 0.55 similarity floor in g3_retrieval.h. The floor
 * is not a tuned constant; it is a measurement taken against this projection.
 * A new mu means a new measurement, not a new header.
 *
 * ==================== RENORMALISE AT USE. NOT OPTIONAL. ====================
 *
 * The transform is  e' = normalize(e - (e . mu_hat) * mu_hat)  where
 * mu_hat = mu / ||mu||  (cm2_floor.py:57-60, and golden_meta.json's formula).
 *
 * THE VALUES BELOW ARE THE npz BITS, WHICH ARE **NOT** mu_hat. The 0.55 floor
 * was measured with mu_hat.
 *
 * Storing the npz bits anyway is deliberate. They are the frozen, committed,
 * CI-verifiable artifact; mu_hat is a derived value whose exact bits depend on
 * the norm's accumulation precision (below), so storing it would bake an
 * implementation detail into the header and make it un-checkable against the
 * npz. Storing mu and renormalising at use reproduces the same TRANSFORM.
 *
 * ---- WHETHER THE RENORMALISATION DOES ANYTHING IS PRECISION-DEPENDENT ----
 *
 * MEASURED over this exact mu (test_embed_mu.py T7 re-derives all of it, so a
 * regeneration cannot leave these numbers stale):
 *
 *   norm accumulated in DOUBLE   -> 0.9999999873, which ROUNDS TO EXACTLY 1.0f
 *                                -> dividing by it moves 0 / 1024 elements.
 *                                   The renormalisation is a NO-OP and you get
 *                                   these stored bits straight back.
 *   norm accumulated in FLOAT32  -> 0.99999994 (numpy) / 0.99999952 (naive
 *                                   sequential) -- neither is 1.0f
 *                                -> dividing by it moves 1024 / 1024 elements,
 *                                   each by exactly 1 ULP.
 *
 * cm2_floor.py ran numpy on a float32 array, so the measurement used the
 * SECOND row. A C implementation that accumulates a 1024-term sum in double --
 * the natural choice -- lands on the FIRST row and gets the stored bits back.
 *
 * CONSEQUENCE, and it is small: 1 ULP is ~6e-8 relative, mu enters the
 * projection quadratically, so the effect on any cosine is ~1e-7 against a
 * floor whose measured margins are two orders of magnitude larger. THE CHOICE
 * IS IMMATERIAL TO THE OPERATING POINT. It is documented so that a later
 * session comparing box output against the Python pipeline does not mistake a
 * 1-ULP difference for a port bug -- the C/M1a GATE 2 lesson, where a tolerance
 * whose provenance was unknown made a correct port look broken.
 *
 * GUIDANCE FOR C/M2b: renormalise, because that is the documented transform,
 * and record which precision the norm was accumulated in. Do NOT skip it on the
 * grounds that ||mu|| is close to 1 -- that happens to be harmless here and it
 * is not the transform that was measured.
 *
 * HONEST LIMIT: bit-exact reproduction of the measurement's INPUTS is therefore
 * not available and is not claimed. What is claimed, and CI-verified against the
 * npz, is that these values are the npz's bits exactly and that the documented
 * transform is the same transform.
 *
 * DIMENSION: 1024, matching ipc/embed_region.h's EMBED_DIM. NOT the 128 of
 * g3_retrieval.h's G3_SEM_DIM_DEFAULT -- mu is applied BEFORE truncation.
 * Both constants are correct and distinct (4f3474f).
 */

#ifndef EMBED_MU_H
#define EMBED_MU_H

#include <stdint.h>
#include "../ipc/embed_region.h"   /* EMBED_DIM - mu lives in the transport's space */

#define EMBED_MU_DIM  1024

/* XOR of the INTENDED binary32 bit patterns, computed by the generator from the
 * npz. test_embed_mu.c recomputes it from the compiled array, which is what
 * turns 'hex literals are exact' from an assumption into a verified property;
 * test_embed_mu.py checks this constant against the npz, so the C side's check
 * chains back to the npz rather than to anything derived from this header. */
#define EMBED_MU_XOR  0x0939EFEAu

static const float EMBED_MU[EMBED_MU_DIM] = {
    -0x1.9af984p-6f, -0x1.ec3a76p-5f, -0x1.0250f2p-6f, -0x1.31c252p-4f,
    0x1.ea9ca6p-5f, 0x1.1aabe4p-8f, 0x1.06b6c4p-6f, -0x1.2e6512p-10f,
    -0x1.3ad0b6p-4f, 0x1.0d433p-5f, -0x1.f81062p-6f, -0x1.44ff9p-6f,
    0x1.f5419p-6f, -0x1.e948b4p-7f, -0x1.3af392p-4f, 0x1.ef51a6p-4f,
    -0x1.14eec2p-9f, 0x1.638db4p-6f, 0x1.474ad4p-4f, 0x1.8225bap-7f,
    -0x1.e3f15cp-7f, 0x1.32da62p-7f, -0x1.07d43ep-6f, 0x1.c1b81p-4f,
    -0x1.e05f1p-6f, 0x1.5c5f8ep-6f, -0x1.2cbb4cp-5f, 0x1.d18704p-5f,
    0x1.4bde34p-7f, -0x1.039bfp-6f, 0x1.bac434p-6f, 0x1.0ab0b2p-5f,
    -0x1.86e38ap-5f, -0x1.2ba2p-6f, -0x1.af35f4p-6f, -0x1.6d1b94p-6f,
    0x1.165d94p-7f, -0x1.314ba2p-5f, -0x1.84479p-7f, 0x1.80c8bp-5f,
    -0x1.49f85ap-7f, 0x1.040edp-8f, 0x1.c763d8p-6f, 0x1.190a62p-9f,
    0x1.9dfbc6p-7f, -0x1.28977ap-5f, 0x1.004826p-5f, -0x1.cc92fp-7f,
    0x1.da0a6ap-6f, 0x1.313552p-8f, -0x1.5c82d4p-5f, -0x1.920e96p-7f,
    -0x1.7d205p-6f, -0x1.fbb082p-8f, -0x1.5acbfep-6f, -0x1.95d778p-5f,
    0x1.bbc86ep-6f, -0x1.97b376p-9f, 0x1.bf6fa8p-6f, -0x1.ea1f52p-6f,
    -0x1.db3188p-5f, 0x1.89c258p-4f, -0x1.247adap-4f, 0x1.026242p-10f,
    0x1.745f12p-7f, 0x1.0c650cp-5f, 0x1.f10616p-7f, -0x1.e0c844p-6f,
    -0x1.152fc6p-4f, -0x1.5c789ap-6f, -0x1.fbfbd6p-8f, -0x1.6e6d9p-6f,
    -0x1.2437a4p-5f, 0x1.26795p-5f, -0x1.dd9234p-8f, 0x1.2ef754p-5f,
    0x1.c13a9ap-7f, -0x1.a660dp-5f, 0x1.85adc6p-6f, 0x1.ac9bf8p-5f,
    -0x1.75acecp-6f, 0x1.f92daep-5f, 0x1.2e733p-8f, 0x1.2078bcp-7f,
    -0x1.e108b2p-7f, 0x1.d3cd42p-5f, 0x1.284a52p-7f, -0x1.7860dcp-6f,
    0x1.4fecaep-7f, 0x1.0bc8b2p-6f, -0x1.b15614p-6f, 0x1.00de1ep-4f,
    -0x1.7e09dap-5f, -0x1.83cfecp-5f, 0x1.bd5a4ep-8f, -0x1.03efep-5f,
    -0x1.0c5c46p-6f, 0x1.184a1cp-11f, -0x1.1d3e18p-5f, -0x1.f80b5ep-7f,
    -0x1.145b9ap-7f, -0x1.99d1b6p-8f, -0x1.a9df9p-6f, -0x1.056ad2p-6f,
    -0x1.3d11p-5f, 0x1.53d9f8p-4f, -0x1.b4a794p-4f, -0x1.f8c212p-7f,
    -0x1.cb870ap-8f, -0x1.5d4ccap-5f, 0x1.444cfep-6f, 0x1.b30312p-6f,
    -0x1.610ae2p-5f, 0x1.f8c72ap-6f, -0x1.b5c42cp-6f, 0x1.5358f2p-11f,
    -0x1.5d4c4ap-6f, -0x1.c22c56p-6f, -0x1.804266p-9f, -0x1.352a1cp-6f,
    0x1.328dbap-7f, 0x1.8bdc64p-8f, -0x1.6b61eap-6f, -0x1.3bfe04p-6f,
    -0x1.6e1bp-6f, 0x1.d75deap-9f, -0x1.8e0f74p-5f, -0x1.db420cp-7f,
    0x1.3c268cp-5f, -0x1.41532p-5f, 0x1.a1932cp-7f, -0x1.258ea4p-7f,
    -0x1.d3b7ecp-9f, -0x1.33f0c4p-4f, 0x1.40ddc4p-6f, 0x1.ad56p-7f,
    0x1.7b57a4p-7f, 0x1.c618dep-8f, 0x1.9dd3b2p-5f, -0x1.3ed502p-7f,
    0x1.e3b168p-8f, -0x1.5728bp-11f, 0x1.76b8a8p-10f, -0x1.03246ep-5f,
    0x1.6e7744p-8f, 0x1.6dc40cp-7f, -0x1.f235f8p-11f, -0x1.4b8692p-7f,
    0x1.410b4ap-7f, -0x1.e87dd8p-10f, 0x1.838382p-6f, 0x1.fb43f4p-8f,
    -0x1.ac56f8p-6f, -0x1.13a7dap-7f, -0x1.315526p-6f, 0x1.149e8ep-5f,
    -0x1.1e9b08p-6f, -0x1.33407cp-7f, 0x1.82c2fep-6f, 0x1.402ddcp-6f,
    0x1.49e7dp-6f, 0x1.9929d2p-8f, 0x1.d8a012p-8f, -0x1.07fb4ep-5f,
    -0x1.5a3536p-6f, 0x1.ee44d2p-7f, -0x1.d2d808p-6f, -0x1.acd1a2p-6f,
    -0x1.215cc4p-7f, -0x1.694abp-6f, -0x1.0350b6p-6f, 0x1.d5258cp-6f,
    0x1.290aecp-7f, -0x1.bc0d4ep-7f, 0x1.6a29dap-6f, -0x1.7bb2cp-5f,
    -0x1.20d06ep-5f, -0x1.93ecdcp-5f, -0x1.349d58p-6f, -0x1.a81956p-7f,
    0x1.27e4c8p-7f, -0x1.9f1e0cp-6f, -0x1.284c76p-8f, 0x1.7ce2fap-12f,
    0x1.062106p-7f, -0x1.0c394p-6f, 0x1.f40268p-9f, -0x1.8e0f1p-7f,
    -0x1.35adbep-5f, -0x1.1e79bep-9f, -0x1.759ap-6f, -0x1.51694ep-5f,
    -0x1.a35c2cp-5f, -0x1.6f1bb8p-7f, -0x1.993806p-6f, -0x1.277e1p-5f,
    0x1.5a62d6p-5f, -0x1.c4d87ap-6f, 0x1.88928cp-7f, -0x1.e9ab08p-6f,
    -0x1.8851fap-7f, 0x1.5bc412p-6f, 0x1.306eb6p-5f, 0x1.d4784ep-7f,
    -0x1.7d980ap-6f, 0x1.f258ap-7f, 0x1.367c44p-6f, -0x1.2b2622p-7f,
    -0x1.f472fep-6f, -0x1.c5ba62p-6f, -0x1.a48eccp-7f, 0x1.68c792p-7f,
    -0x1.24f1dep-6f, 0x1.69f1bp-6f, -0x1.34f116p-7f, 0x1.70e974p-7f,
    -0x1.0c12e2p-5f, 0x1.91b946p-6f, 0x1.2f2422p-9f, 0x1.6dd388p-5f,
    0x1.71db24p-5f, 0x1.e5ea1cp-7f, -0x1.15a136p-6f, 0x1.d415dcp-9f,
    -0x1.0d1d88p-6f, -0x1.9bc9e6p-5f, 0x1.0b525ap-5f, -0x1.9e89d4p-6f,
    0x1.4b172cp-8f, 0x1.744edap-7f, -0x1.2d33fep-9f, -0x1.b6d0acp-6f,
    0x1.241a32p-5f, 0x1.01e5c6p-5f, 0x1.46606ap-4f, -0x1.8f4858p-7f,
    0x1.2d7f5cp-8f, 0x1.d9e288p-6f, 0x1.41083ep-5f, 0x1.8c9d44p-9f,
    0x1.71600ep-8f, -0x1.9dc8acp-20f, 0x1.5d4776p-10f, 0x1.441676p-8f,
    -0x1.5cabd6p-5f, 0x1.56e5d6p-5f, -0x1.5d1a7ap-7f, 0x1.0d44d4p-7f,
    -0x1.f4347p-6f, 0x1.10f456p-6f, 0x1.dda29p-6f, -0x1.c7a144p-4f,
    -0x1.6bf698p-5f, 0x1.ea48f4p-6f, 0x1.3415dp-6f, -0x1.6c0eeep-7f,
    -0x1.745f1p-7f, -0x1.46f126p-7f, 0x1.08f65cp-7f, 0x1.09a0dcp-6f,
    0x1.9b651ep-6f, -0x1.6f396cp-5f, -0x1.f0c7bep-7f, 0x1.0a8e64p-5f,
    0x1.6375b4p-6f, 0x1.0c91aep-6f, -0x1.53ce28p-6f, 0x1.e2e048p-7f,
    -0x1.a014f2p-5f, -0x1.d34176p-6f, -0x1.848c74p-6f, 0x1.1e0844p-6f,
    -0x1.61f0ep-6f, -0x1.ff507cp-8f, 0x1.2ffc3ap-6f, 0x1.5ad854p-7f,
    0x1.2b5f8cp-7f, -0x1.a7026p-6f, 0x1.0ca0cp-5f, 0x1.46ae18p-4f,
    -0x1.663b38p-6f, -0x1.02ebfcp-6f, -0x1.6795dap-6f, -0x1.c7409ep-7f,
    0x1.79358p-9f, 0x1.bc59dap-5f, 0x1.29279ap-5f, 0x1.2fc3eap-7f,
    -0x1.c168a8p-8f, -0x1.935ecap-6f, 0x1.8298aep-6f, -0x1.44594ep-9f,
    -0x1.3d7116p-5f, 0x1.8920aap-5f, -0x1.143d22p-6f, 0x1.1838b6p-7f,
    0x1.d6eba6p-8f, 0x1.986596p-4f, 0x1.d9c7bep-7f, -0x1.cb73bcp-6f,
    -0x1.8241b2p-6f, -0x1.1ea9a4p-5f, -0x1.d050bep-9f, 0x1.c8c734p-8f,
    0x1.32634p-5f, -0x1.d052f4p-9f, 0x1.fe3102p-6f, -0x1.682f2cp-5f,
    0x1.90605p-6f, -0x1.ff2408p-5f, 0x1.a47372p-4f, 0x1.a57764p-7f,
    -0x1.0d486ap-7f, -0x1.8cedd4p-8f, -0x1.6aae86p-6f, -0x1.769afep-6f,
    -0x1.059468p-5f, 0x1.02c67p-4f, -0x1.3c4278p-6f, 0x1.85000ap-8f,
    -0x1.c0f2bap-7f, 0x1.12069ep-5f, -0x1.8ba2f6p-6f, -0x1.4e470cp-6f,
    0x1.5c422p-9f, 0x1.a55e2ep-7f, -0x1.1a0ce4p-6f, 0x1.23430cp-5f,
    -0x1.61cb76p-8f, 0x1.37364ep-7f, -0x1.7235f2p-7f, -0x1.3c2fdap-7f,
    0x1.76876cp-5f, 0x1.5d2f38p-8f, 0x1.86363ap-7f, 0x1.1bfa6ep-5f,
    0x1.7ebaeap-5f, 0x1.8d2856p-7f, 0x1.f07f2ap-6f, 0x1.7d1eb2p-8f,
    -0x1.590364p-8f, 0x1.8d18d2p-9f, -0x1.89a3p-8f, -0x1.5dbb0cp-5f,
    -0x1.f04226p-5f, -0x1.3cd614p-5f, -0x1.293e18p-6f, 0x1.4b3a48p-5f,
    0x1.3d9212p-6f, 0x1.4001e2p-6f, -0x1.1b107cp-5f, 0x1.c3ac6cp-7f,
    0x1.5ddc0ep-9f, -0x1.c97664p-8f, -0x1.c71f4p-5f, -0x1.e502f2p-6f,
    -0x1.71c538p-8f, 0x1.8cc584p-6f, -0x1.a4c5e4p-6f, -0x1.a11c86p-7f,
    -0x1.6532aep-6f, -0x1.f2d402p-4f, 0x1.8c1406p-5f, -0x1.3a673ep-6f,
    -0x1.43edbap-5f, -0x1.ff73ap-7f, -0x1.0b4c58p-7f, -0x1.99c314p-5f,
    0x1.55d5aep-4f, -0x1.3bdde4p-8f, -0x1.852b6p-4f, -0x1.4a6402p-5f,
    -0x1.673f2p-8f, 0x1.cacf2ep-8f, 0x1.531148p-4f, 0x1.e2c9fp-6f,
    0x1.f36f2cp-5f, 0x1.d98912p-8f, -0x1.b096d6p-9f, 0x1.ed46e8p-8f,
    0x1.0a57d8p-5f, 0x1.0d91dcp-6f, -0x1.10ee18p-11f, 0x1.5e3f26p-7f,
    0x1.4dce6ap-8f, -0x1.dedc2cp-6f, -0x1.f87c7p-8f, 0x1.2812cep-8f,
    -0x1.23294p-8f, -0x1.bb730ep-8f, 0x1.e2ebdp-6f, -0x1.076f24p-5f,
    0x1.5af476p-6f, -0x1.7e8b34p-6f, 0x1.5d5edep-5f, 0x1.17d4fcp-5f,
    -0x1.e020acp-4f, -0x1.81b3acp-6f, -0x1.44734p-6f, 0x1.22e552p-9f,
    0x1.6be31ap-7f, 0x1.f7430ep-9f, -0x1.e9f634p-5f, -0x1.814b4p-9f,
    -0x1.bbfea2p-7f, -0x1.c5b1a8p-5f, -0x1.2f6caap-8f, -0x1.cdbd28p-7f,
    0x1.6079a2p-6f, 0x1.ba5152p-8f, 0x1.e5639cp-5f, -0x1.c43614p-11f,
    -0x1.4d6bap-6f, -0x1.67a994p-5f, 0x1.b4c738p-6f, -0x1.b3cd04p-6f,
    0x1.0915p-4f, -0x1.968208p-6f, -0x1.1b16f4p-5f, 0x1.a79228p-6f,
    -0x1.a96ep-6f, 0x1.6828c6p-5f, -0x1.260daap-4f, -0x1.59c504p-5f,
    -0x1.9e162ep-7f, -0x1.7e4836p-8f, -0x1.96fe04p-5f, -0x1.a99efp-5f,
    -0x1.3d0886p-9f, 0x1.2b4a08p-4f, -0x1.0306dp-7f, -0x1.2abc38p-9f,
    -0x1.06cb4p-7f, 0x1.7369f8p-7f, -0x1.0310aep-5f, -0x1.7f572ap-6f,
    0x1.1a9b54p-4f, -0x1.15dcb6p-8f, -0x1.05a13cp-6f, 0x1.36c324p-7f,
    -0x1.a39324p-6f, 0x1.72839cp-5f, -0x1.183d24p-6f, -0x1.f43f18p-5f,
    -0x1.3ba2c4p-7f, -0x1.627d58p-7f, -0x1.02aa32p-5f, 0x1.301a06p-6f,
    0x1.27ef92p-9f, 0x1.16434ap-8f, 0x1.65db84p-5f, -0x1.c3b3ep-6f,
    0x1.830bbcp-8f, 0x1.2f0eb6p-5f, -0x1.514e1ep-5f, -0x1.3f339p-5f,
    -0x1.7cd798p-7f, 0x1.eebec8p-8f, 0x1.7eabe6p-7f, 0x1.99531ep-5f,
    -0x1.0bf55cp-7f, 0x1.d7fdccp-6f, 0x1.c08ceep-10f, -0x1.0364c2p-5f,
    0x1.d4919ep-7f, 0x1.a142cp-11f, -0x1.305c96p-7f, -0x1.0cb0b2p-7f,
    0x1.15a4eap-6f, -0x1.0c113ap-7f, 0x1.7006ecp-8f, 0x1.225c12p-5f,
    0x1.96c02p-9f, -0x1.da8decp-7f, 0x1.289fd4p-6f, 0x1.d0c85cp-8f,
    -0x1.2e63b4p-4f, -0x1.07de02p-5f, -0x1.aea6eap-5f, 0x1.100718p-6f,
    0x1.8ad772p-6f, -0x1.87216ep-6f, 0x1.750caap-6f, -0x1.2d805ep-4f,
    0x1.4f10d6p-4f, -0x1.1e7a2ap-5f, -0x1.d5ec2p-7f, -0x1.59647cp-10f,
    -0x1.bdc098p-5f, -0x1.32df22p-7f, 0x1.7dc5aep-7f, 0x1.7c5394p-6f,
    -0x1.674096p-6f, -0x1.aad376p-5f, -0x1.c4b776p-9f, 0x1.00153p-7f,
    -0x1.85a42ap-6f, -0x1.f4a156p-6f, -0x1.e1625ep-4f, 0x1.4b39c8p-6f,
    -0x1.96930ap-11f, 0x1.c73d18p-6f, -0x1.9fbb7ep-9f, 0x1.cfcc8p-6f,
    -0x1.0820f2p-5f, -0x1.27c94ep-6f, -0x1.389fc2p-7f, -0x1.422b78p-7f,
    0x1.f84e92p-7f, 0x1.c71722p-6f, 0x1.a383f8p-8f, -0x1.1693cep-5f,
    -0x1.cb9ff8p-8f, 0x1.82a946p-6f, -0x1.36ce9ap-7f, -0x1.14b68p-8f,
    0x1.c06928p-7f, 0x1.7f073p-6f, 0x1.01b758p-10f, 0x1.fb0ba2p-6f,
    0x1.85ed94p-7f, 0x1.5bc14ep-7f, 0x1.116832p-5f, -0x1.ebb0fep-6f,
    0x1.38c58cp-10f, 0x1.4300eep-4f, 0x1.01a3c6p-6f, -0x1.7d0a64p-8f,
    0x1.ef7d4p-7f, 0x1.bb23a2p-6f, 0x1.3b9c74p-6f, 0x1.9db15ep-5f,
    0x1.67247ap-6f, 0x1.38a93p-6f, 0x1.ff49ecp-11f, 0x1.1e43fap-6f,
    0x1.0dd9a8p-5f, -0x1.113b62p-9f, 0x1.1d4a9ep-6f, -0x1.a7d764p-7f,
    0x1.be0812p-7f, -0x1.46783ap-6f, -0x1.634f3ap-5f, -0x1.27e58ep-5f,
    0x1.ed5b1p-6f, -0x1.2d8e1ep-6f, -0x1.2818a8p-5f, 0x1.c82bd2p-7f,
    -0x1.7bdb34p-6f, 0x1.07afcp-5f, 0x1.cfecfap-6f, -0x1.d5480ap-6f,
    0x1.4a9e9p-4f, -0x1.4d1f1cp-5f, 0x1.ded974p-6f, -0x1.189422p-6f,
    0x1.327164p-5f, 0x1.652d3p-8f, 0x1.25f2dcp-5f, -0x1.359fe6p-5f,
    -0x1.301636p-6f, 0x1.36650ep-7f, 0x1.0c6782p-8f, 0x1.67af2ep-13f,
    -0x1.5e6854p-9f, -0x1.e3b29cp-8f, 0x1.478b2ep-6f, 0x1.0b377ap-5f,
    0x1.72166p-5f, -0x1.f1e694p-14f, 0x1.da4878p-5f, -0x1.15dabp-5f,
    0x1.f69ef6p-7f, 0x1.f03448p-6f, 0x1.a02764p-7f, -0x1.a7cc74p-5f,
    -0x1.b40d98p-7f, 0x1.10d2aep-5f, 0x1.6256c8p-8f, -0x1.75d746p-5f,
    0x1.807cc6p-11f, 0x1.a345c6p-7f, -0x1.0a3072p-9f, -0x1.1ce436p-9f,
    -0x1.582508p-6f, -0x1.43f39p-6f, 0x1.304bb8p-6f, 0x1.d19b3cp-6f,
    0x1.4a17e6p-7f, -0x1.178e5ep-7f, -0x1.3e4c96p-8f, -0x1.0431c4p-4f,
    -0x1.db3544p-7f, 0x1.a2fe34p-7f, 0x1.4f12a4p-4f, -0x1.70d51cp-6f,
    -0x1.6dfeccp-5f, -0x1.0ffc9p-6f, 0x1.1cc092p-7f, 0x1.1d9478p-7f,
    0x1.c30846p-6f, 0x1.993a6ap-6f, 0x1.4d1e64p-7f, 0x1.45376ap-6f,
    -0x1.c75516p-5f, 0x1.6bfe7ap-6f, -0x1.18328ap-4f, 0x1.27f078p-9f,
    -0x1.8b051p-6f, 0x1.4943c2p-6f, -0x1.a01c82p-6f, 0x1.d5b272p-7f,
    -0x1.339e42p-5f, 0x1.773d68p-5f, 0x1.8b743ep-12f, -0x1.a1a29ep-5f,
    -0x1.e7a5p-6f, 0x1.17d8fap-5f, 0x1.a567d4p-9f, -0x1.0a2484p-4f,
    0x1.51b074p-9f, -0x1.597cd8p-6f, 0x1.b6f88p-7f, 0x1.1b2f5p-4f,
    0x1.a285d2p-6f, 0x1.04bc44p-5f, 0x1.9be738p-9f, -0x1.f52196p-6f,
    -0x1.965bd8p-6f, -0x1.c598d4p-6f, -0x1.6999a4p-5f, 0x1.ad8c38p-14f,
    -0x1.e8f30cp-6f, 0x1.bc118ep-7f, 0x1.396354p-6f, 0x1.ba9d4ep-11f,
    -0x1.11abc2p-6f, -0x1.c8fc3ep-8f, 0x1.0eff48p-5f, -0x1.a92deap-6f,
    -0x1.0e851p-7f, -0x1.474cb8p-6f, -0x1.a08074p-7f, 0x1.74fedap-12f,
    0x1.a646fp-7f, 0x1.814032p-6f, -0x1.401c34p-5f, 0x1.07a36p-5f,
    0x1.9df22cp-5f, -0x1.c43bfp-6f, 0x1.7f0674p-5f, 0x1.a7e944p-6f,
    0x1.7a771ap-6f, -0x1.347a32p-9f, -0x1.9bacbap-6f, 0x1.ba17fep-6f,
    -0x1.24d5b8p-6f, 0x1.a32344p-5f, 0x1.37bd98p-5f, -0x1.2cd4a8p-5f,
    0x1.5327f2p-8f, -0x1.036a26p-4f, 0x1.390438p-7f, -0x1.8ef3e2p-5f,
    -0x1.e5d748p-6f, -0x1.0e280cp-4f, 0x1.265f5cp-4f, 0x1.379b8cp-5f,
    0x1.2742ecp-5f, -0x1.e89b88p-6f, -0x1.56424ep-7f, -0x1.10d53cp-5f,
    -0x1.6b79ep-6f, 0x1.2d8b3cp-7f, -0x1.66229ep-7f, -0x1.0e1952p-5f,
    -0x1.c11ffap-5f, -0x1.e9f56cp-6f, -0x1.76e4cp-6f, -0x1.a5169ep-5f,
    -0x1.cf88e6p-6f, -0x1.05b2f2p-5f, 0x1.e524ecp-6f, -0x1.b4b4p-5f,
    0x1.f3bcc4p-6f, -0x1.ca80bp-6f, -0x1.4b4b4p-5f, 0x1.b3f9e4p-6f,
    0x1.36c634p-6f, -0x1.176d04p-5f, -0x1.f50498p-5f, -0x1.b5ae64p-7f,
    0x1.43493ep-6f, -0x1.120c1cp-5f, -0x1.c87a3ap-10f, 0x1.443ba8p-6f,
    -0x1.24017ep-6f, -0x1.f522c2p-8f, -0x1.102862p-6f, -0x1.422fa2p-6f,
    0x1.a2eea6p-8f, -0x1.7fadf8p-6f, -0x1.57616p-6f, 0x1.145a3p-5f,
    0x1.87c934p-7f, 0x1.1f0d54p-6f, -0x1.149ac4p-4f, -0x1.15783p-5f,
    -0x1.127dd4p-5f, -0x1.36d644p-6f, 0x1.254ab4p-6f, -0x1.aeb17p-9f,
    -0x1.9c2a2ep-8f, -0x1.964cecp-7f, 0x1.04010ap-5f, 0x1.bba526p-6f,
    -0x1.b17f42p-7f, 0x1.c6a936p-12f, -0x1.039e4p-6f, -0x1.6e261ap-7f,
    0x1.fc0b7ep-6f, -0x1.b0b976p-7f, 0x1.2175b4p-4f, 0x1.c1063ep-5f,
    -0x1.cd9adap-9f, -0x1.474828p-8f, 0x1.89fea2p-5f, 0x1.c1b5c8p-5f,
    0x1.19afa2p-5f, -0x1.316d8ep-6f, 0x1.1d68c2p-8f, -0x1.801d7p-6f,
    0x1.34bd48p-5f, -0x1.d08accp-6f, 0x1.1e747ep-6f, 0x1.7406c4p-6f,
    0x1.4df6aep-4f, 0x1.22283ep-6f, 0x1.388e5ep-7f, -0x1.8bd628p-11f,
    0x1.7e1a42p-5f, 0x1.b39e7cp-7f, -0x1.a5268cp-7f, -0x1.3a476p-5f,
    0x1.c01ac4p-7f, 0x1.6cc636p-6f, -0x1.55fec4p-6f, -0x1.9a6ac6p-6f,
    0x1.2a5f86p-5f, 0x1.580004p-6f, 0x1.df904cp-5f, -0x1.91fcc2p-6f,
    0x1.731884p-8f, 0x1.9913b4p-5f, -0x1.f728b2p-5f, -0x1.941d7cp-5f,
    -0x1.77afeap-6f, 0x1.3c743cp-5f, 0x1.cf4526p-6f, 0x1.e21f9ep-6f,
    -0x1.a4b256p-6f, -0x1.ef3d96p-7f, -0x1.63dd06p-11f, 0x1.c2baa6p-7f,
    0x1.cdbb7ep-5f, -0x1.8d147cp-6f, 0x1.ddd8bp-6f, -0x1.7f6ca8p-5f,
    -0x1.c6b6c6p-7f, 0x1.2c9bbap-8f, -0x1.355c26p-10f, -0x1.11887ap-9f,
    0x1.2fdde8p-7f, 0x1.d35a02p-5f, 0x1.cf7ea2p-6f, 0x1.0c8ff2p-11f,
    0x1.31a684p-5f, -0x1.816aacp-6f, -0x1.942d04p-6f, -0x1.84fec8p-7f,
    0x1.16cfdp-6f, -0x1.b0c144p-7f, 0x1.56054cp-6f, -0x1.e3e4d4p-7f,
    -0x1.7c8a4ep-5f, 0x1.66f782p-5f, 0x1.435e9p-6f, 0x1.758922p-7f,
    0x1.c57ba8p-5f, 0x1.9decc4p-5f, 0x1.0d1572p-5f, 0x1.6dc66ap-8f,
    -0x1.d5bc3cp-7f, 0x1.2dff7p-5f, -0x1.e2d3e2p-7f, 0x1.ae23b4p-5f,
    0x1.8d0c6ap-9f, 0x1.499916p-5f, -0x1.13ec2p-6f, 0x1.39ac66p-6f,
    -0x1.e2e79cp-7f, 0x1.8a6ed4p-5f, -0x1.e7432cp-6f, -0x1.55334cp-7f,
    0x1.5952d2p-6f, -0x1.703188p-7f, -0x1.11fbecp-5f, 0x1.07d0cep-6f,
    -0x1.7cc02ep-7f, -0x1.68b4a2p-8f, 0x1.ba933ap-5f, 0x1.08679ep-4f,
    0x1.de9bbep-9f, 0x1.61d97cp-7f, 0x1.d0bc44p-13f, 0x1.267a0ap-8f,
    -0x1.6cb0fep-6f, 0x1.568dd4p-5f, -0x1.c6bf0ap-5f, -0x1.23b992p-10f,
    -0x1.41198ep-6f, 0x1.d9cffap-7f, 0x1.23cdbep-5f, 0x1.b7553p-6f,
    -0x1.7b372cp-6f, -0x1.32b994p-6f, 0x1.912fbep-6f, 0x1.284168p-6f,
    -0x1.b125fcp-5f, -0x1.a392fp-8f, 0x1.ff7576p-7f, -0x1.38fa08p-5f,
    0x1.66c002p-6f, 0x1.b1ac74p-6f, 0x1.ab1598p-5f, 0x1.b832eap-6f,
    -0x1.f1077p-6f, -0x1.3201b4p-9f, 0x1.b06202p-8f, 0x1.e51f86p-6f,
    0x1.5977c4p-5f, -0x1.226d28p-6f, 0x1.80a7bep-7f, 0x1.28dae8p-6f,
    -0x1.45f90cp-5f, 0x1.2c58fap-6f, -0x1.fcf1d8p-8f, 0x1.a84556p-6f,
    0x1.02246p-8f, -0x1.3540a8p-8f, 0x1.9b9438p-6f, 0x1.d87784p-5f,
    -0x1.7f401cp-6f, 0x1.068c9ap-8f, -0x1.cfb59cp-5f, 0x1.5d3788p-5f,
    0x1.f77a92p-10f, 0x1.76272ep-7f, -0x1.5f4ca4p-5f, 0x1.48b37cp-5f,
    -0x1.f470f2p-7f, -0x1.b057bp-8f, -0x1.d9360cp-5f, -0x1.5a6c64p-6f,
    -0x1.8d5d46p-8f, 0x1.22e42cp-5f, -0x1.526b0ap-9f, 0x1.7e9c38p-5f,
    -0x1.decd14p-7f, 0x1.d6ce38p-8f, 0x1.0959b4p-7f, -0x1.724aa2p-7f,
    0x1.debb1ep-8f, 0x1.7532a8p-5f, -0x1.e7eefp-5f, -0x1.9e35a8p-7f,
    0x1.8e8ec2p-8f, 0x1.4755cep-6f, 0x1.946102p-9f, 0x1.bd509cp-5f,
    -0x1.44a8dep-6f, 0x1.1f1f92p-5f, 0x1.6c27ccp-6f, -0x1.35fefp-6f,
    -0x1.4bb502p-5f, -0x1.cafcfep-12f, 0x1.30567ap-9f, 0x1.6c447cp-7f,
    -0x1.b9eec8p-6f, -0x1.b3685ep-7f, 0x1.0b20a8p-6f, -0x1.e82484p-5f,
    0x1.4ff42ap-7f, 0x1.b6bb98p-12f, -0x1.5db614p-4f, -0x1.0d7cd8p-6f,
    0x1.7dfe1cp-5f, 0x1.3eeeb2p-8f, -0x1.bceb84p-10f, -0x1.f257fep-6f,
    0x1.d51f36p-7f, -0x1.c923e2p-7f, 0x1.2b6944p-7f, -0x1.bb7d6p-5f,
    -0x1.a0cefp-6f, -0x1.809ab2p-7f, 0x1.7947ep-6f, -0x1.9b8d82p-9f,
    -0x1.9112cep-6f, 0x1.8346f4p-7f, -0x1.918186p-6f, 0x1.9ee192p-5f,
    0x1.a1e672p-8f, 0x1.64d1fap-10f, -0x1.0668d6p-6f, -0x1.7d03dap-7f,
    -0x1.08afb6p-4f, -0x1.31be28p-6f, 0x1.671f88p-7f, -0x1.32ff86p-6f,
    0x1.77a4fp-8f, 0x1.135992p-7f, 0x1.ae570ep-7f, 0x1.2a9f04p-7f,
    -0x1.ba400ep-7f, -0x1.055ep-5f, 0x1.1c37ccp-6f, -0x1.246e68p-7f,
    -0x1.4977c2p-7f, 0x1.d24c4p-6f, 0x1.dfbc68p-7f, -0x1.3407b8p-6f,
    0x1.5ea894p-8f, 0x1.fba57p-5f, -0x1.3ab666p-8f, -0x1.33da54p-6f,
    0x1.03b452p-6f, -0x1.361d42p-5f, 0x1.15af56p-6f, -0x1.331e28p-8f,
    0x1.2c6da4p-5f, -0x1.5a0cdp-6f, 0x1.c1344cp-7f, 0x1.a365a8p-6f,
    0x1.237236p-5f, -0x1.53386cp-5f, 0x1.300f4p-6f, -0x1.eeac1ep-10f,
    0x1.e24b3ap-6f, 0x1.1816f4p-7f, 0x1.f46f44p-11f, 0x1.218218p-8f,
    0x1.1ae9bap-10f, 0x1.bf949cp-7f, 0x1.241f0cp-6f, -0x1.1bf8fap-4f,
    -0x1.e09f84p-6f, 0x1.f7c27ap-8f, -0x1.5596f2p-10f, 0x1.da877ap-8f,
    0x1.53288p-7f, -0x1.d891a4p-9f, 0x1.68aefep-7f, 0x1.17babap-5f,
    0x1.2878aap-5f, 0x1.16cdb4p-7f, -0x1.eb6a92p-15f, 0x1.22cca4p-5f,
    0x1.5f663ep-5f, -0x1.7bd066p-6f, 0x1.3a214ep-7f, -0x1.056058p-7f,
    0x1.266116p-7f, 0x1.200dbep-5f, 0x1.859a96p-7f, 0x1.7b6ca4p-7f,
    -0x1.5dfb02p-6f, -0x1.2e7014p-11f, -0x1.b3775cp-7f, -0x1.7f877ap-5f,
    -0x1.f0b734p-6f, -0x1.9d1bdcp-9f, 0x1.fa977p-7f, 0x1.08b2aep-5f,
    0x1.e384f2p-7f, -0x1.a139d2p-6f, 0x1.585182p-7f, 0x1.5f53f6p-5f,
    0x1.86d1acp-8f, -0x1.a3b704p-5f, 0x1.b8ca6p-6f, 0x1.561d32p-5f,
    -0x1.5270cp-8f, -0x1.898c9cp-8f, -0x1.9f204p-9f, 0x1.f9151p-7f,
    -0x1.7f74d2p-7f, 0x1.246d78p-6f, 0x1.b13b6ep-6f, 0x1.cb114ep-6f,
    0x1.4de448p-6f, 0x1.166232p-9f, -0x1.d0a046p-10f, 0x1.085966p-5f,
    0x1.8c766p-8f, -0x1.1270c8p-10f, 0x1.cf798cp-9f, 0x1.01e014p-6f,
    0x1.b93f7cp-10f, -0x1.c8f3ap-9f, -0x1.c2544cp-9f, 0x1.0819f2p-6f,
    0x1.3a3a42p-7f, -0x1.be5628p-7f, -0x1.d55dbap-14f, -0x1.1beee4p-7f,
    0x1.36c6aap-9f, -0x1.25311ap-8f, 0x1.7ddb66p-7f, 0x1.99bceep-10f,
    0x1.51fba8p-9f, -0x1.64c9dp-7f, 0x1.477c3ap-8f, -0x1.64a69ep-7f,
};

_Static_assert(sizeof(EMBED_MU) / sizeof(EMBED_MU[0]) == EMBED_MU_DIM,
               "mu array length must be EMBED_MU_DIM");
_Static_assert(EMBED_MU_DIM == 1024, "mu is a 1024-dimension artifact");
/* mu is applied in the TRANSPORT's space, before truncation - so it must track
 * EMBED_DIM, and a change to either must break this build rather than silently
 * project in the wrong space. */
_Static_assert(EMBED_MU_DIM == (int)EMBED_DIM,
               "mu dimension must equal the transport EMBED_DIM (pre-truncation)");

#endif /* EMBED_MU_H */
