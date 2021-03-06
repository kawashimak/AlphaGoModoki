﻿#include "position.hpp"
#include "move.hpp"
#include "generateMoves.hpp"

#include "mate.h"

const constexpr size_t MaxCheckMoves = 73;

// 詰み探索用のMovePicker
template <bool or_node, bool INCHECK>
class MovePicker {
public:
	explicit MovePicker(const Position& pos) {
		if (or_node) {
			last_ = generateMoves<Check>(moveList_, pos);
			if (INCHECK) {
				// 自玉が王手の場合、逃げる手かつ王手をかける手を生成
				ExtMove* curr = moveList_;
				const Bitboard pinned = pos.pinnedBB();
				while (curr != last_) {
					if (!pos.pseudoLegalMoveIsEvasion(curr->move, pinned))
						curr->move = (--last_)->move;
					else
						++curr;
				}
			}
		}
		else {
			last_ = generateMoves<Evasion>(moveList_, pos);
			// 玉の移動による自殺手と、pinされている駒の移動による自殺手を削除
			ExtMove* curr = moveList_;
			const Bitboard pinned = pos.pinnedBB();
			while (curr != last_) {
				if (!pos.pseudoLegalMoveIsLegal<false, false>(curr->move, pinned))
					curr->move = (--last_)->move;
				else
					++curr;
			}
		}
		assert(size() <= MaxCheckMoves);
	}
	size_t size() const { return static_cast<size_t>(last_ - moveList_); }
	ExtMove* begin() { return &moveList_[0]; }
	ExtMove* end() { return last_; }
	bool empty() const { return size() == 0; }

private:
	ExtMove moveList_[MaxCheckMoves];
	ExtMove* last_;
};

// 2手詰めチェック
// 手番側が王手されていること
FORCE_INLINE bool mateMoveIn2Ply(Position& pos)
{
	// AND節点

	// すべてのEvasionについて
	const CheckInfo ci(pos);
	for (const auto& ml : MovePicker<false, false>(pos)) {
		//std::cout << " " << ml.move().toUSI() << std::endl;
		if (pos.moveGivesCheck(ml.move, ci))
			return false;

		// 1手動かす
		StateInfo state;
		pos.doMove(ml.move, state, ci, false);

		// 1手詰めかどうか
		if (pos.mateMoveIn1Ply() == Move::moveNone()) {
			// 1手詰めでない場合
			// 詰みが見つからなかった時点で終了
			pos.undoMove(ml.move);
			return false;
		}

		pos.undoMove(ml.move);
	}
	return true;
}

// 3手詰めチェック
// 手番側が王手でないこと
template <bool INCHECK>
FORCE_INLINE bool mateMoveIn3Ply(Position& pos)
{
	// OR節点

	// すべての合法手について
	const CheckInfo ci(pos);
	for (const auto& ml : MovePicker<true, INCHECK>(pos)) {
		// 1手動かす
		StateInfo state;
		pos.doMove(ml.move, state, ci, true);

		//std::cout << ml.move().toUSI() << std::endl;
		// 王手の場合
		// 2手詰めチェック
		if (mateMoveIn2Ply(pos)) {
			// 詰みが見つかった時点で終了
			pos.undoMove(ml.move);
			return true;
		}

		pos.undoMove(ml.move);
	}
	return false;
}

// 奇数手詰めチェック
// 手番側が王手でないこと
// 詰ます手を返すバージョン
Move mateMoveInOddPlyReturnMove(Position& pos, const int depth) {
	// OR節点

	// すべての合法手について
	const CheckInfo ci(pos);
	for (const auto& ml : MovePicker<true, false>(pos)) {
		// 1手動かす
		StateInfo state;
		pos.doMove(ml.move, state, ci, true);

		// 千日手チェック
		int isDraw = 0;
		switch (pos.isDraw(16)) {
		case NotRepetition: break;
		case RepetitionDraw:
		case RepetitionWin:
		case RepetitionLose:
		case RepetitionSuperior:
		case RepetitionInferior:
		{
			pos.undoMove(ml.move);
			continue;
		}
		default: UNREACHABLE;
		}

		//std::cout << ml.move().toUSI() << std::endl;
		// 偶数手詰めチェック
		if (mateMoveInEvenPly(pos, depth - 1)) {
			// 詰みが見つかった時点で終了
			pos.undoMove(ml.move);
			return ml.move;
		}

		pos.undoMove(ml.move);
	}
	return Move::moveNone();
}

// 奇数手詰めチェック
// 手番側が王手でないこと
template <bool INCHECK = false>
bool mateMoveInOddPly(Position& pos, const int depth)
{
	// OR節点

	// すべての合法手について
	const CheckInfo ci(pos);
	for (const auto& ml : MovePicker<true, INCHECK>(pos)) {
		// 1手動かす
		StateInfo state;
		pos.doMove(ml.move, state, ci, true);

		//std::cout << ml.move().toUSI() << std::endl;
		// 王手の場合
		// 偶数手詰めチェック
		if (mateMoveInEvenPly(pos, depth - 1)) {
			// 詰みが見つかった時点で終了
			pos.undoMove(ml.move);
			return true;
		}

		pos.undoMove(ml.move);
	}
	return false;
}

// 偶数手詰めチェック
// 手番側が王手されていること
bool mateMoveInEvenPly(Position& pos, const int depth)
{
	// AND節点

	// すべてのEvasionについて
	const CheckInfo ci(pos);
	for (const auto& ml : MovePicker<false, false>(pos)) {
		//std::cout << " " << ml.move().toUSI() << std::endl;
		const bool givesCheck = pos.moveGivesCheck(ml.move, ci);

		// 1手動かす
		StateInfo state;
		pos.doMove(ml.move, state, ci, givesCheck);

		if (depth == 4) {
			// 3手詰めかどうか
			if (givesCheck? !mateMoveIn3Ply<true>(pos) : !mateMoveIn3Ply<false>(pos)) {
				// 3手詰めでない場合
				// 詰みが見つからなかった時点で終了
				pos.undoMove(ml.move);
				return false;
			}
		}
		else {
			// 奇数手詰めかどうか
			if (givesCheck ? !mateMoveInOddPly<true>(pos, depth - 1) : !mateMoveInOddPly<false>(pos, depth - 1)) {
				// 偶数手詰めでない場合
				// 詰みが見つからなかった時点で終了
				pos.undoMove(ml.move);
				return false;
			}
		}

		pos.undoMove(ml.move);
	}
	return true;
}
