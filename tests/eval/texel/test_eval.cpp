#ifdef TEXEL_TUNING

#include "engine/eval/pos_eval.hpp"
#include "engine/eval/tuning/eval_features.hpp"
#include "engine/eval/virtual_board.hpp"
#include "engine/config/eval.hpp"
#include "gtest/gtest.h"
#include <string>
#include <array>
#include <iostream>

class TexelTuningEvalTest : public ::testing::Test
{
protected:
    void SetUp() override {}
};

// 1. Test de consistance globale (ton test original avec logs de debug)
TEST_F(TexelTuningEvalTest, EvalConsistency)
{
    const std::array<std::string, 3> fens = {
        "8/8/1kq5/8/4R3/3QK3/8/8 w - - 0 1",
        "rn1qkbnr/pp2pppp/2p5/5b2/3PN3/8/PPP2PPP/R1BQKBNR w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"};

    for (const auto &fen : fens)
    {
        VBoard b;
        b.load_fen(fen);

        // Calcul via la fonction officielle de recherche
        int eval_score = Eval::eval(b, -engine_constants::eval::Inf, engine_constants::eval::Inf);

        // Calcul via l'extracteur de features du tuner
        EvalFeatures f = Eval::extract_eval_features(b);
        int texel_eval = static_cast<int>(Eval::score_eval_features(f, b));

        if (texel_eval != eval_score)
        {
            std::cout << "\n[FAILED CONSISTENCY] FEN: " << fen << std::endl;
            std::cout << "  Eval (Incrémental) : " << eval_score << std::endl;
            std::cout << "  Tuner (Statique)    : " << texel_eval << std::endl;
            std::cout << "  Différence          : " << eval_score - texel_eval << std::endl;
            std::cout << "  Phase Board         : " << (int)b.get_eval_state().phase << std::endl;
        }

        ASSERT_EQ(texel_eval, eval_score) << "Échec de consistance sur FEN: " << fen;
    }
}

// 2. Test de la symétrie des Buckets PSQT (Vérifie si le miroir noir ^56 est correct)
TEST_F(TexelTuningEvalTest, BucketSymmetry)
{
    VBoard b_white;
    b_white.load_fen("8/8/8/8/8/8/8/4K3 w - - 0 1"); // Roi blanc en e1 -> Bucket 1

    VBoard b_black;
    b_black.load_fen("4k3/8/8/8/8/8/8/8 b - - 0 1"); // Roi noir en e8 -> Doit être Bucket 1 miroir

    int bucket_w = engine_constants::eval::PSQTBucketLayout[b_white.king_sq[WHITE]];
    int bucket_b = engine_constants::eval::PSQTBucketLayout[b_black.king_sq[BLACK] ^ 56];

    EXPECT_EQ(bucket_w, bucket_b) << "La symétrie des Buckets entre Blancs et Noirs est cassée !";
}

// 3. Test de la Mobilité (Vérifie la saturation à 27 et le compte des cases)
TEST_F(TexelTuningEvalTest, MobilityConsistency)
{
    // Position où la dame a énormément de cases
    VBoard b;
    b.load_fen("8/8/8/q7/8/8/8/K1k5 b - - 0 1");

    EvalFeatures f = Eval::extract_eval_features(b);

    // On vérifie si la somme des indices de mobilité extraits correspond à 1 pièce
    double knight_count = 0, bishop_count = 0, rook_count = 0, queen_count = 0;
    for (int i = 0; i < 9; ++i)
        knight_count += std::abs(f.knight_mob[i]);
    for (int i = 0; i < 14; ++i)
        bishop_count += std::abs(f.bishop_mob[i]);
    for (int i = 0; i < 15; ++i)
        rook_count += std::abs(f.rook_mob[i]);
    for (int i = 0; i < 28; ++i)
        queen_count += std::abs(f.queen_mob[i]);

    // Dans cette position, seule la dame et le roi sont présents pour les noirs
    EXPECT_EQ(queen_count, 1.0) << "L'extracteur n'a pas trouvé exactement une Dame pour la mobilité";
}

// 4. Test des Menaces (Threats)
TEST_F(TexelTuningEvalTest, ThreatsConsistency)
{
    VBoard b;
    // La dame blanche attaque la tour noire non protégée
    b.load_fen("8/1k6/8/8/8/2Q5/3r4/3K4 w - - 0 1");

    EvalFeatures f = Eval::extract_eval_features(b);

    // Vérifie si la menace Dame attaque Tour est capturée
    // undefendedThreatsBonus[QUEEN][ROOK]
    bool found = false;
    if (f.undefended_threats[QUEEN][ROOK] != 0)
        found = true;

    EXPECT_TRUE(found) << "Le tuner n'a pas détecté la menace Dame attaque Tour non défendue";
}

// 5. Test de calcul de la Phase
TEST_F(TexelTuningEvalTest, PhaseConsistency)
{
    VBoard b;
    b.load_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");

    EvalFeatures f = Eval::extract_eval_features(b);
    // Dans score_eval_features, la phase doit correspondre à EvalState::phase (24 au début)
    EXPECT_EQ((int)b.get_eval_state().phase, 24);
}

TEST_F(TexelTuningEvalTest, ComponentDecomposition)
{
    // La position qui pose problème
    std::string fen = "8/8/1kq5/8/4R3/3QK3/8/8 w - - 0 1";
    VBoard b;
    b.load_fen(fen);

    // Extraction des caractéristiques
    EvalFeatures f = Eval::extract_eval_features(b);

    // On va comparer les sous-totaux un par un
    // NOTE : Ces fonctions/accès doivent exister ou être simulés pour le test

    // A. Matériel + PST (Incrémental vs Statique)
    const EvalState &state = b.get_eval_state();
    int wk = state.king_sq[WHITE];
    int bk = state.king_sq[BLACK];
    int wb = engine_constants::eval::PSQTBucketLayout[wk];
    int bb = engine_constants::eval::PSQTBucketLayout[bk ^ 56];

    int incremental_mg_base = (state.mg_pst[WHITE][wb] + state.pieces_val[WHITE]) -
                              (state.mg_pst[BLACK][bb] + state.pieces_val[BLACK]);

    // Calcul manuel depuis les features (statique)
    // On simule ce que score_eval_features fait pour les PST
    double static_mg_pst = 0;
    for (int p = PAWN; p <= KING; ++p)
    {
        for (int sq = 0; sq < 64; ++sq)
        {
            if (f.mg_pst[p][sq] != 0)
            {
                // Si coeff > 0 (blanc), on utilise wb, sinon bb
                int bkt = (f.mg_pst[p][sq] > 0) ? wb : bb;
                static_mg_pst += f.mg_pst[p][sq] * engine_constants::eval::mg_tables[p][bkt][sq];
            }
        }
    }
    double static_material = 0;
    for (int p = PAWN; p <= QUEEN; ++p)
        static_material += f.material[p] * engine_constants::eval::pieces_score[p];

    EXPECT_EQ(incremental_mg_base, (int)(static_mg_pst + static_material)) << "Divergence dans le bloc PST/Matériel";

    // B. Le coupable probable : Les Menaces (Threats)
    // Dans eval(), threats_score est ajouté APRES l'interpolation ou aux deux MG/EG ?
    // On vérifie si eval() et score_eval_features traitent threats_score de la même façon.

    int threats_white = Eval::evaluate_structured_threats(WHITE, b);
    int threats_black = Eval::evaluate_structured_threats(BLACK, b);
    int expected_threats = threats_white - threats_black;

    double tuner_threats = 0;
    // On récupère ce que le tuner a extrait pour les menaces
    for (int a = 0; a < 6; ++a)
    {
        for (int v = 0; v < 6; ++v)
        {
            tuner_threats += f.defended_threats[a][v] * engine_constants::eval::defendedThreatsBonus[a][v];
            tuner_threats += f.undefended_threats[a][v] * engine_constants::eval::undefendedThreatsBonus[a][v];
        }
    }

    EXPECT_EQ(expected_threats, (int)tuner_threats) << "Divergence dans le bloc Menaces (Threats)";

    // C. Vérification de la division finale
    // C'est ici que les 4 points se cachent souvent (int vs double)
    int phase = state.phase;
    int mg_final = incremental_mg_base + expected_threats; // Hypothèse : ajouté au MG
    int eg_final = incremental_mg_base + expected_threats; // Hypothèse : ajouté au EG

    int eval_formula = (mg_final * phase + eg_final * (24 - phase)) / 24;

    // Si on fait le calcul en double (Tuner)
    double tuner_formula = ((double)mg_final * phase + (double)eg_final * (24 - phase)) / 24.0;

    if (eval_formula != (int)tuner_formula)
    {
        std::cout << "[DEBUG] Erreur d'arrondi détectée !" << std::endl;
        std::cout << "  Formule Int    : " << eval_formula << std::endl;
        std::cout << "  Formule Double : " << tuner_formula << std::endl;
    }
}

TEST_F(TexelTuningEvalTest, HypothesisThreatsInterpolation)
{
    // On utilise les données de ton échec : Phase = 10
    int phase = 10;

    // Supposons qu'une menace (ex: Dame attaque Roi) vaut 10 points
    int threat_val = 10;

    // --- LOGIQUE EVAL() ---
    // Dans eval(), tu ajoutes le score aux deux accumulateurs, puis tu divises.
    int mg_eval = threat_val;
    int eg_eval = threat_val;
    int final_eval = (mg_eval * phase + eg_eval * (24 - phase)) / 24;
    // Mathématiquement : (10 * 10 + 10 * 14) / 24 = 240 / 24 = 10

    // --- LOGIQUE TUNER (ERREUR POTENTIELLE) ---
    // Si le Tuner n'applique la menace qu'au Midgame (ou oublie le Endgame)
    double mg_tuner = threat_val;
    double eg_tuner = 0; // Oubli du endgame ou traitement asymétrique
    double final_tuner_v1 = (mg_tuner * phase + eg_tuner * (24 - phase)) / 24.0;
    // Calcul : (10 * 10 + 0 * 14) / 24 = 100 / 24 = 4.16 -> (int) 4

    // --- COMPARAISON ---
    std::cout << "[HYPOTHÈSE] Si menace = 10 et Phase = 10 :" << std::endl;
    std::cout << "  Résultat Eval (Symétrique) : " << final_eval << std::endl;
    std::cout << "  Résultat Tuner (MG uniquement) : " << (int)final_tuner_v1 << std::endl;
    std::cout << "  Écart observé : " << (final_eval - (int)final_tuner_v1) << std::endl;

    // Si l'écart est de 6 (ou proche de 4 selon la valeur du bonus), on a trouvé le bug.
}
#endif