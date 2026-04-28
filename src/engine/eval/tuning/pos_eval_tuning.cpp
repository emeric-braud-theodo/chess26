#ifdef TEXEL_TUNING

#include "engine/eval/pos_eval.hpp"
#include "core/move/generator/move_generator.hpp"

// Transforme un bitboard de pions en un masque où chaque bit
// représente une colonne (file) occupée (8 bits utilisés).
inline uint8_t get_pawn_files(U64 pawns)
{
    pawns |= (pawns >> 32);
    pawns |= (pawns >> 16);
    pawns |= (pawns >> 8);
    return static_cast<uint8_t>(pawns & 0xFF);
}

namespace Eval
{
    static void accumulate_structured_threat_features(Color us, const VBoard &board, EvalFeatures &f, double sign)
    {
        const Color them = (Color)!us;
        const U64 occupied = board.get_occupancy(NO_COLOR);

        std::array<U64, constants::PieceTypeCount> our_attacks{};
        std::array<U64, constants::PieceTypeCount> enemy_attacks{};

        auto accumulate_attacks = [&](Color side, std::array<U64, constants::PieceTypeCount> &attacks)
        {
            U64 pawns = board.get_piece_bitboard(side, PAWN);
            while (pawns)
            {
                const int sq = cpu::pop_lsb(pawns);
                attacks[PAWN] |= side == WHITE ? MoveGen::PawnAttacksWhite[sq] : MoveGen::PawnAttacksBlack[sq];
            }

            for (int piece = KNIGHT; piece <= QUEEN; ++piece)
            {
                U64 bb = board.get_piece_bitboard(side, piece);
                while (bb)
                {
                    const int sq = cpu::pop_lsb(bb);

                    U64 moves;
                    if (piece == KNIGHT)
                        moves = MoveGen::KnightAttacks[sq];
                    else if (piece == BISHOP)
                        moves = MoveGen::generate_bishop_moves(sq, occupied);
                    else if (piece == ROOK)
                        moves = MoveGen::generate_rook_moves(sq, occupied);
                    else
                        moves = MoveGen::generate_bishop_moves(sq, occupied) | MoveGen::generate_rook_moves(sq, occupied);

                    attacks[piece] |= moves;
                }
            }

            attacks[KING] = MoveGen::KingAttacks[board.king_sq[side]];
        };

        accumulate_attacks(us, our_attacks);
        accumulate_attacks(them, enemy_attacks);

        U64 enemy_defended_squares = 0;
        for (int piece = PAWN; piece <= KING; ++piece)
            enemy_defended_squares |= enemy_attacks[piece];

        const U64 enemy_pieces = board.get_occupancy(them);

        for (int piece = KNIGHT; piece <= QUEEN; ++piece)
        {
            U64 threats = our_attacks[piece] & enemy_pieces;

            U64 defended = threats & enemy_defended_squares;
            while (defended)
            {
                const int sq = cpu::pop_lsb(defended);
                const Piece victim_piece = board.get_piece_on_square(sq).second;
                f.defended_threats[piece][victim_piece] += sign;
            }

            U64 undefended = threats & ~enemy_defended_squares;
            while (undefended)
            {
                const int sq = cpu::pop_lsb(undefended);
                const Piece victim_piece = board.get_piece_on_square(sq).second;
                f.undefended_threats[piece][victim_piece] += sign;
            }
        }

        const U64 our_pawns = board.get_piece_bitboard(us, PAWN);
        U64 pawn_pushes = us == WHITE
                              ? (our_pawns << 8) & ~occupied
                              : (our_pawns >> 8) & ~occupied;

        const U64 double_pushes = us == WHITE
                                      ? (((pawn_pushes & 0x0000000000FF0000ULL) << 8) & ~occupied)
                                      : (((pawn_pushes & 0x0000FF0000000000ULL) >> 8) & ~occupied);
        pawn_pushes |= double_pushes;

        const U64 safe_pushes = pawn_pushes & ~enemy_defended_squares;
        const U64 non_pawn_enemies = enemy_pieces & ~board.get_piece_bitboard(them, PAWN);

        const U64 push_attacks = us == WHITE
                                     ? (((safe_pushes & ~core::mask::File[0]) << 7) | ((safe_pushes & ~core::mask::File[7]) << 9))
                                     : (((safe_pushes & ~core::mask::File[7]) >> 7) | ((safe_pushes & ~core::mask::File[0]) >> 9));

        f.pawn_push_threats += sign * std::popcount(push_attacks & non_pawn_enemies);
    }

    static void accumulate_castling_and_safety_features(Color us, const VBoard &board, EvalFeatures &f, double sign)
    {
        const Color them = (Color)!us;
        const int king_sq = board.king_sq[us];
        const int king_file = king_sq & 7;

        const U64 our_pawns = board.get_piece_bitboard(us, PAWN);
        const U64 enemy_pawns = board.get_piece_bitboard(them, PAWN);
        const U64 enemy_heavies = board.get_piece_bitboard(them, ROOK) | board.get_piece_bitboard(them, QUEEN);

        uint8_t our_files = get_pawn_files(our_pawns);
        uint8_t enemy_files = get_pawn_files(enemy_pawns);
        uint8_t vicinity = core::mask::KingVicinityFile[king_file];

        uint8_t open = vicinity & ~our_files & ~enemy_files;
        uint8_t semi_open = vicinity & ~our_files & enemy_files;

        const int open_count = std::popcount(static_cast<uint8_t>(open));
        const int semi_open_count = std::popcount(static_cast<uint8_t>(semi_open));

        U64 open_files_bb = 0;
        U64 semi_files_bb = 0;

        for (int file = king_file - 1; file <= king_file + 1; ++file)
        {
            int64_t valid_file_mask = -static_cast<int64_t>(file >= 0 && file <= 7);
            int clamped_f = file & 7;

            open_files_bb |= (core::mask::File[clamped_f] & -static_cast<int64_t>((open >> clamped_f) & 1)) & valid_file_mask;
            semi_files_bb |= (core::mask::File[clamped_f] & -static_cast<int64_t>((semi_open >> clamped_f) & 1)) & valid_file_mask;
        }

        const int heavy_open_count = std::popcount(enemy_heavies & open_files_bb);
        const int heavy_semi_open_count = std::popcount(enemy_heavies & semi_files_bb);

        f.open_files_near_king += sign * open_count;
        f.semi_open_files_near_king += sign * semi_open_count;
        f.heavy_on_open += sign * heavy_open_count;
        f.heavy_on_semi_open += sign * heavy_semi_open_count;
    }

    static void accumulate_pawn_features(Color color, const VBoard &board, EvalFeatures &f, double sign)
    {
        const U64 our_pawns = board.get_piece_bitboard(color, PAWN);
        const U64 enemy_pawns = board.get_piece_bitboard(!color, PAWN);

        U64 doubled_mask = our_pawns & ((our_pawns >> 8) | (our_pawns >> 16) | (our_pawns >> 24) |
                                        (our_pawns >> 32) | (our_pawns >> 40) | (our_pawns >> 48) | (our_pawns >> 56));
        int num_doubled_files = std::popcount(get_pawn_files(doubled_mask));
        f.doubled_files += sign * num_doubled_files;

        uint8_t files = get_pawn_files(our_pawns);
        uint8_t iso_files = files & ~((files << 1) | (files >> 1));
        int num_iso = std::popcount(iso_files);
        f.isolated_files += sign * num_iso;

        U64 temp_pawns = our_pawns;
        while (temp_pawns)
        {
            const int sq = cpu::pop_lsb(temp_pawns);

            if (!(enemy_pawns & masks.passed[color][sq]))
            {
                const int rank = sq >> 3;
                const int relative_rank = (color == WHITE) ? rank : (7 - rank);

                f.passed_mg[relative_rank] += sign;
                f.passed_eg[relative_rank] += sign;
            }
        }
    }

    static void accumulate_mobility_features(Color us, const VBoard &board, EvalFeatures &f, double sign)
    {
        const U64 occ_all = board.get_occupancy(NO_COLOR);
        const U64 our_occ = board.get_occupancy(us);

        for (int piece = KNIGHT; piece <= QUEEN; ++piece)
        {
            U64 bb = board.get_piece_bitboard(us, piece);

            while (bb)
            {
                const int sq = cpu::pop_lsb(bb);

                U64 moves;
                if (piece == KNIGHT)
                    moves = MoveGen::KnightAttacks[sq];
                else if (piece == BISHOP)
                    moves = MoveGen::generate_bishop_moves(sq, occ_all);
                else if (piece == ROOK)
                    moves = MoveGen::generate_rook_moves(sq, occ_all);
                else
                    moves = MoveGen::generate_bishop_moves(sq, occ_all) | MoveGen::generate_rook_moves(sq, occ_all);

                const int count = std::popcount(moves & ~our_occ);

                if (piece == KNIGHT)
                    f.knight_mob[count] += sign;
                else if (piece == BISHOP)
                    f.bishop_mob[count] += sign;
                else if (piece == ROOK)
                    f.rook_mob[count] += sign;
                else
                    f.queen_mob[std::min(count, 27)] += sign;
            }
        }
    }

    static void accumulate_bishop_pair_features(Color us, const VBoard &board, EvalFeatures &f, double sign)
    {
        if (std::popcount(board.get_piece_bitboard(us, BISHOP)) >= 2)
        {
            f.bishop_pair_mg += sign;
            f.bishop_pair_eg += sign;
        }
    }

    static void accumulate_material_features(Color us, const VBoard &board, EvalFeatures &f, double sign)
    {
        for (int piece = PAWN; piece <= QUEEN; ++piece)
        {
            const int count = std::popcount(board.get_piece_bitboard(us, piece));
            f.material[piece] += sign * count;
        }
    }

    static void accumulate_mopup_features(const VBoard &board, EvalFeatures &f)
    {
        const EvalState &state = board.get_eval_state();

        const double eg_score = (state.eg_pst[WHITE] + state.pieces_val[WHITE]) -
                                (state.eg_pst[BLACK] + state.pieces_val[BLACK]);

        if (std::abs(eg_score) <= 200.0)
            return;

        const Color winner = (eg_score > 0.0) ? WHITE : BLACK;
        const Color loser = (Color)!winner;

        const int wk = board.king_sq[WHITE];
        const int bk = board.king_sq[BLACK];

        const int loser_king = board.king_sq[loser];
        const int k_file = loser_king & 7;
        const int k_rank = loser_king >> 3;

        const int dist_from_center = std::max(std::abs(k_file - 3), std::abs(k_rank - 3));
        const int dist_between_kings = king_distance(wk, bk);

        const double sign = (winner == WHITE) ? 1.0 : -1.0;
        f.king_dist_center += sign * dist_from_center;
        f.king_closeness += sign * (engine_constants::eval::maxDistBetweenKings - dist_between_kings);
    }

    static void accumulate_pst_features(Color us, const VBoard &board, EvalFeatures &f, double sign)
    {
        for (int piece = PAWN; piece <= KING; ++piece)
        {
            U64 bb = board.get_piece_bitboard(us, piece);

            while (bb)
            {
                const int sq = cpu::pop_lsb(bb);
                const int psq = us == WHITE ? sq : sq ^ 56;

                f.mg_pst[piece][psq] += sign;
                f.eg_pst[piece][psq] += sign;
            }
        }
    }

    EvalFeatures extract_eval_features(const VBoard &board)
    {
        EvalFeatures f{};

        accumulate_pawn_features(WHITE, board, f, +1.0);
        accumulate_pawn_features(BLACK, board, f, -1.0);

        accumulate_castling_and_safety_features(WHITE, board, f, +1.0);
        accumulate_castling_and_safety_features(BLACK, board, f, -1.0);

        accumulate_structured_threat_features(WHITE, board, f, +1.0);
        accumulate_structured_threat_features(BLACK, board, f, -1.0);

        accumulate_bishop_pair_features(WHITE, board, f, +1.0);
        accumulate_bishop_pair_features(BLACK, board, f, -1.0);

        accumulate_mobility_features(WHITE, board, f, +1.0);
        accumulate_mobility_features(BLACK, board, f, -1.0);

        accumulate_material_features(WHITE, board, f, +1.0);
        accumulate_material_features(BLACK, board, f, -1.0);

        accumulate_mopup_features(board, f);

        accumulate_pst_features(WHITE, board, f, +1.0);
        accumulate_pst_features(BLACK, board, f, -1.0);

        return f;
    }

    double score_eval_features(const EvalFeatures &f, const VBoard &board)
    {
        const EvalState &state = board.get_eval_state();
        using namespace engine_constants::eval;

        // 1. Détermination des buckets PSQT (Crucial pour la consistance)
        const int white_king_sq = state.king_sq[WHITE];
        const int black_king_sq = state.king_sq[BLACK];
        const int white_bucket = PSQTBucketLayout[white_king_sq];
        const int black_bucket = PSQTBucketLayout[black_king_sq ^ 56];

        // 2. Base PST (Incrémentale) utilisant les bons buckets
        double mg = static_cast<double>(state.mg_pst[WHITE][white_bucket]) -
                    static_cast<double>(state.mg_pst[BLACK][black_bucket]);

        double eg = static_cast<double>(state.eg_pst[WHITE][white_bucket]) -
                    static_cast<double>(state.eg_pst[BLACK][black_bucket]);

        // 3. Matériel
        for (int i = PAWN; i <= QUEEN; ++i)
        {
            double val = f.material[i] * pieces_score[i];
            mg += val;
            eg += val;
        }

        // 4. Structure de pions
        mg += f.doubled_files * doubledFilesMgMalus;
        eg += f.doubled_files * doubledFilesEgMalus;

        mg += f.isolated_files * isolatedFilesMgMalus;
        eg += f.isolated_files * isolatedFilesEgMalus;

        // 5. King Safety (Sécurité du Roi)
        mg += f.open_files_near_king * openFileMalus;
        mg += f.semi_open_files_near_king * semiOpenFileMalus;
        mg += f.heavy_on_open * heavyEnemiesOpenFileMalus;
        mg += f.heavy_on_semi_open * heavyEnemiesSemiOpenFileMalus;

        // 6. Menaces (Threats) - Ajoutées au MG ET EG pour matcher Eval::eval
        for (int attacker = 0; attacker < constants::PieceTypeCount; ++attacker)
        {
            for (int victim = 0; victim < constants::PieceTypeCount; ++victim)
            {
                double bonus_def = f.defended_threats[attacker][victim] * defendedThreatsBonus[attacker][victim];
                double bonus_undef = f.undefended_threats[attacker][victim] * undefendedThreatsBonus[attacker][victim];

                double total_threat = bonus_def + bonus_undef;
                mg += total_threat;
                eg += total_threat;
            }
        }

        double p_push = f.pawn_push_threats * pawnPushThreatBonus;
        mg += p_push;
        eg += p_push;

        // 7. Paire de Fous
        mg += f.bishop_pair_mg * bishopPairMgBonus;
        eg += f.bishop_pair_eg * bishopPairEgBonus;

        // 8. Mop-up (Endgame only)
        eg += f.king_dist_center * kingDistFromCenterBonus;
        eg += f.king_closeness * closeKingBonus;

        // 9. Pions passés
        for (int i = 0; i < 8; ++i)
        {
            mg += f.passed_mg[i] * passed_bonus_mg[i];
            eg += f.passed_eg[i] * passed_bonus_eg[i];
        }

        // 10. Mobilité (Ajoutée au MG ET EG pour matcher Eval::eval)
        for (int i = 0; i < 9; ++i)
        {
            double val = f.knight_mob[i] * knight_mob[i];
            mg += val;
            eg += val;
        }
        for (int i = 0; i < 14; ++i)
        {
            double val = f.bishop_mob[i] * bishop_mob[i];
            mg += val;
            eg += val;
        }
        for (int i = 0; i < 15; ++i)
        {
            double val = f.rook_mob[i] * rook_mob[i];
            mg += val;
            eg += val;
        }
        for (int i = 0; i < 28; ++i)
        {
            double val = f.queen_mob[i] * queen_mob[i];
            mg += val;
            eg += val;
        }

        // 11. Interpolation finale avec précision entière identique à Eval::eval
        // On arrondit d'abord les totaux MG/EG en entiers pour simuler l'arithmétique du moteur
        int final_mg = static_cast<int>(std::round(mg));
        int final_eg = static_cast<int>(std::round(eg));

        return static_cast<double>((final_mg * state.phase + final_eg * (totalPhase - state.phase)) / totalPhase);
    }
}

#endif