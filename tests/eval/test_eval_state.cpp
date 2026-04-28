#include "engine/eval/pos_eval.hpp"
#include "gtest/gtest.h"
#include "core/move/generator/move_generator.hpp"
#include "engine/eval/virtual_board.hpp"

class EvalStateTest : public ::testing::Test
{
protected:
    void SetUp() override {}
};

// Fonction utilitaire pour comparer les PST buckets entre deux EvalState
void expect_pst_eq(const EvalState &a, const EvalState &b, Color c, const std::string &msg)
{
    for (int bkt = 0; bkt < engine_constants::eval::PSQT_BUCKET_N; ++bkt)
    {
        EXPECT_EQ(a.mg_pst[c][bkt], b.mg_pst[c][bkt])
            << msg << " (MG Bucket " << bkt << ", Color " << (c == WHITE ? "White" : "Black") << ")";
        EXPECT_EQ(a.eg_pst[c][bkt], b.eg_pst[c][bkt])
            << msg << " (EG Bucket " << bkt << ", Color " << (c == WHITE ? "White" : "Black") << ")";
    }
}

TEST_F(EvalStateTest, IncrementalConsistency)
{
    VBoard b;
    // Position complexe avec roques possibles, promotions et captures EP
    b.load_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");

    MoveList list;
    MoveGen::generate_legal_moves(b, list);

    for (int i = 0; i < list.count; ++i)
    {
        Move m = list[i];

        // 1. Sauvegarde de l'état initial (on copie les tableaux de scores)
        int16_t initial_mg_w[engine_constants::eval::PSQT_BUCKET_N];
        for (int bkt = 0; bkt < engine_constants::eval::PSQT_BUCKET_N; ++bkt)
            initial_mg_w[bkt] = b.get_eval_state().mg_pst[WHITE][bkt];

        uint64_t initial_pawn_key = b.get_eval_state().pawn_key;

        // 2. Jouer le coup
        b.play(m);

        // 3. Calcul "Brut" pour vérifier
        EvalState static_eval(b.get_all_bitboards());

        // 4. Vérification de l'incrément pour chaque camp et chaque bucket
        expect_pst_eq(b.get_eval_state(), static_eval, WHITE, "Incohérence MG/EG");
        expect_pst_eq(b.get_eval_state(), static_eval, BLACK, "Incohérence MG/EG");

        EXPECT_EQ(b.get_eval_state().phase, static_eval.phase)
            << "Phase incohérente après coup: " << m.to_uci();
        EXPECT_EQ(b.get_eval_state().pawn_key, static_eval.pawn_key)
            << "Pawn Key incohérente après coup: " << m.to_uci();

        // 5. Unplay et vérification du retour à l'état initial
        b.unplay(m);
        for (int bkt = 0; bkt < engine_constants::eval::PSQT_BUCKET_N; ++bkt)
        {
            EXPECT_EQ(b.get_eval_state().mg_pst[WHITE][bkt], initial_mg_w[bkt])
                << "Erreur de décrémentation PST après unplay: " << m.to_uci() << " Bucket: " << bkt;
        }
        EXPECT_EQ(b.get_eval_state().pawn_key, initial_pawn_key)
            << "Erreur de décrémentation PawnKey après unplay: " << m.to_uci();
    }
}

TEST_F(EvalStateTest, ConsistencyLongSequence)
{
    VBoard b;
    b.load_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");

    for (int i = 0; i < 10; i++)
    {
        MoveList list;
        MoveGen::generate_legal_moves(b, list);
        if (list.count == 0)
            break;

        Move m = list[0];
        b.play(m);
    }

    EvalState incremental = b.get_eval_state();
    EvalState static_recalc(b.get_all_bitboards());

    // Vérifications multi-buckets
    for (int bkt = 0; bkt < engine_constants::eval::PSQT_BUCKET_N; ++bkt)
    {
        ASSERT_EQ(incremental.mg_pst[WHITE][bkt], static_recalc.mg_pst[WHITE][bkt]) << "Erreur MG PST White Bucket " << bkt;
        ASSERT_EQ(incremental.eg_pst[WHITE][bkt], static_recalc.eg_pst[WHITE][bkt]) << "Erreur EG PST White Bucket " << bkt;
        ASSERT_EQ(incremental.mg_pst[BLACK][bkt], static_recalc.mg_pst[BLACK][bkt]) << "Erreur MG PST Black Bucket " << bkt;
        ASSERT_EQ(incremental.eg_pst[BLACK][bkt], static_recalc.eg_pst[BLACK][bkt]) << "Erreur EG PST Black Bucket " << bkt;
    }

    ASSERT_EQ(incremental.phase, static_recalc.phase) << "Erreur Phase";
    ASSERT_EQ(incremental.pawn_key, static_recalc.pawn_key) << "Erreur Pawn Key";
}