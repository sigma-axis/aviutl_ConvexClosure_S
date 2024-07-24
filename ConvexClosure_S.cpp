/*
The MIT License (MIT)

Copyright (c) 2024 sigma-axis

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the “Software”), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <cstdint>
#include <algorithm>
#include <numeric>
#include <string>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <commdlg.h>

using byte = int8_t;
#include <exedit/Filter.hpp>
#include <exedit/CommandId.hpp>

#include "multi_thread.hpp"
#include "exedit_memory.hpp"
#include "relative_path.hpp"
#include "tiled_image.hpp"

using i16 = int16_t;
using i32 = int32_t;


////////////////////////////////
// 仕様書．
////////////////////////////////
struct check_data {
	enum def : int32_t {
		unchecked = 0,
		checked = 1,
		button = -1,
		dropdown = -2,
	};
};

#define PLUGIN_VERSION	"v0.10-alpha1"
#define PLUGIN_AUTHOR	"sigma-axis"
#define FILTER_INFO_FMT(name, ver, author)	(name##" "##ver##" by "##author)
#define FILTER_INFO(name)	constexpr char filter_name[] = name, info[] = FILTER_INFO_FMT(name, PLUGIN_VERSION, PLUGIN_AUTHOR)
FILTER_INFO("凸包σ");

// trackbars.
constexpr char const* track_names[] = { "余白", "透明度", "内透明度", "αしきい値", "画像X", "画像Y" };
constexpr auto track_name_invalid = "----";
constexpr int32_t
	track_den[]      = {   1,   10,   10,   10,     1,     1 },
	track_min[]      = {   0,    0,    0,    0, -4000, -4000 },
	track_min_drag[] = {   0,    0,    0,    0, -1000, -1000 },
	track_def[]      = {   0,    0,    0,  500,     0,     0 },
	track_max_drag[] = { 500, 1000, 1000, 1000, +1000, +1000 },
	track_max[]	     = { 500, 1000, 1000, 1000, +4000, +4000 };
constexpr int track_link[] = { 0, 0, 0, 0, 1, -1, };

namespace idx_track
{
	enum id : int {
		extend,
		transp,
		f_transp,
		threshold,
		img_x,
		img_y,
	};
	constexpr int count_entries = std::size(track_names);
};

static_assert(
	std::size(track_names) == std::size(track_den) &&
	std::size(track_names) == std::size(track_min) &&
	std::size(track_names) == std::size(track_min_drag) &&
	std::size(track_names) == std::size(track_def) &&
	std::size(track_names) == std::size(track_max_drag) &&
	std::size(track_names) == std::size(track_max) &&
	std::size(track_names) == std::size(track_link));

// checks.
constexpr char const* check_names[]
	= { "アンチエイリアス", "背景色の設定", "パターン画像ファイル" };
constexpr int32_t
	check_default[] = { check_data::checked, check_data::button, check_data::button };
namespace idx_check
{
	enum id : int {
		antialias,
		color,
		file,
	};
	constexpr int count_entries = std::size(check_names);
};
static_assert(std::size(check_names) == std::size(check_default));

constexpr auto color_format = L"RGB ( %d , %d , %d )";
constexpr size_t size_col_fmt = std::wstring_view{ color_format }.size() + 1
	+ 3 * (std::size(L"255") - std::size(L"%d"));

// exdata.
constexpr ExEdit::ExdataUse exdata_use[] =
{
	{ .type = ExEdit::ExdataUse::Type::Binary, .size = 3, .name = "color" },
	{ .type = ExEdit::ExdataUse::Type::Padding, .size = 1, .name = nullptr },
	{ .type = ExEdit::ExdataUse::Type::String, .size = 256, .name = "file" },
};
namespace idx_data
{
	namespace _impl
	{
		static consteval size_t idx(auto name) {
			auto ret = std::find_if(std::begin(exdata_use), std::end(exdata_use),
				[name](auto d) { return d.name != nullptr && std::string_view{ d.name } == name; }) - exdata_use;
			if (ret < std::size(exdata_use)) return ret;
			std::unreachable();
		}
	}
	enum id : int {
		color = _impl::idx("color"),
		file = _impl::idx("file"),
	};
	constexpr int count_entries = 3;
}
//#pragma pack(push, 1)
struct Exdata {
	ExEdit::Exdata::ExdataColor color{ .r = 0, .g = 0, .b = 0 };
	char file[exdata_use[idx_data::file].size]{};
};

//#pragma pack(pop)
constexpr static Exdata exdata_def = {};

static_assert(sizeof(Exdata) == std::accumulate(
	std::begin(exdata_use), std::end(exdata_use), size_t{ 0 }, [](auto v, auto d) { return v + d.size; }));

// callbacks.
BOOL func_proc(ExEdit::Filter* efp, ExEdit::FilterProcInfo* efpip);
BOOL func_WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam, AviUtl::EditHandle* editp, ExEdit::Filter* efp);
int32_t func_window_init(HINSTANCE hinstance, HWND hwnd, int y, int base_id, int sw_param, ExEdit::Filter* efp);
static inline BOOL func_init(ExEdit::Filter* efp) { exedit.init(efp->exedit_fp); return TRUE; }


static inline constinit ExEdit::Filter filter = {
	.flag = ExEdit::Filter::Flag::Effect,
	.name = const_cast<char*>(filter_name),
	.track_n = std::size(track_names),
	.track_name = const_cast<char**>(track_names),
	.track_default = const_cast<int*>(track_def),
	.track_s = const_cast<int*>(track_min),
	.track_e = const_cast<int*>(track_max),
	.check_n = std::size(check_names),
	.check_name = const_cast<char**>(check_names),
	.check_default = const_cast<int*>(check_default),
	.func_proc = &func_proc,
	.func_init = &func_init,
	.func_WndProc = &func_WndProc,
	.exdata_size = sizeof(exdata_def),
	.information = const_cast<char*>(info),
	.func_window_init = &func_window_init,
	.exdata_def = const_cast<Exdata*>(&exdata_def),
	.exdata_use = exdata_use,
	.track_scale = const_cast<int*>(track_den),
	.track_link = const_cast<int*>(track_link),
	.track_drag_min = const_cast<int*>(track_min_drag),
	.track_drag_max = const_cast<int*>(track_max_drag),
};


////////////////////////////////
// ウィンドウ状態の保守．
////////////////////////////////
/*
efp->exfunc->get_hwnd(efp->processing, i, j):
	i = 0:		j 番目のスライダーの中央ボタン．
	i = 1:		j 番目のスライダーの左トラックバー．
	i = 2:		j 番目のスライダーの右トラックバー．
	i = 3:		j 番目のチェック枠のチェックボックス．
	i = 4:		j 番目のチェック枠のボタン．
	i = 5, 7:	j 番目のチェック枠の右にある static (テキスト).
	i = 6:		j 番目のチェック枠のコンボボックス．
	otherwise -> nullptr.
*/
static inline void update_extendedfilter_wnd(ExEdit::Filter* efp)
{
	auto* exdata = reinterpret_cast<Exdata*>(efp->exdata_ptr);

	// whether the background pattern image is specified, or single color.
	wchar_t col_fmt[size_col_fmt] = L"";
	auto file = "", img_x = track_names[idx_track::img_x], img_y = track_names[idx_track::img_y];
	if (exdata->file[0] == '\0') {
		// single color.
		::swprintf_s(col_fmt, color_format,
			exdata->color.r, exdata->color.g, exdata->color.b);
		img_x = img_y = track_name_invalid;
	}
	else {
		// background image.
		std::end(exdata->file)[-1] = '\0';
		file = relative_path::ptr_file_name(exdata->file);
	}

	// choose button text from "画像X/Y" or "----".
	::SetWindowTextA(efp->exfunc->get_hwnd(efp->processing, 0, idx_track::img_x), img_x);
	::SetWindowTextA(efp->exfunc->get_hwnd(efp->processing, 0, idx_track::img_y), img_y);

	// set label text next to the buttons.
	::SetWindowTextW(efp->exfunc->get_hwnd(efp->processing, 5, idx_check::color), col_fmt);
	::SetWindowTextA(efp->exfunc->get_hwnd(efp->processing, 5, idx_check::file), file);
}

BOOL func_WndProc(HWND, UINT message, WPARAM wparam, LPARAM lparam, AviUtl::EditHandle*, ExEdit::Filter* efp)
{
	if (message != ExEdit::ExtendedFilter::Message::WM_EXTENDEDFILTER_COMMAND) return FALSE;

	auto* exdata = reinterpret_cast<Exdata*>(efp->exdata_ptr);
	auto chk = static_cast<idx_check::id>(wparam >> 16);
	auto cmd = wparam & 0xffff;

	switch (cmd) {
		using namespace ExEdit::ExtendedFilter::CommandId;
	case EXTENDEDFILTER_PUSH_BUTTON:
		switch (chk) {
		case idx_check::color:
		{
			efp->exfunc->set_undo(efp->processing, 0);
			char const heading = std::exchange(exdata->file[0], '\0');
			if (efp->exfunc->x6c(efp, &exdata->color, 0x002)) { // color_dialog
				std::memset(exdata->file, 0, sizeof(exdata->file));
				exedit.update_any_exdata(efp->processing, exdata_use[idx_data::color].name);
				exedit.update_any_exdata(efp->processing, exdata_use[idx_data::file].name);

				update_extendedfilter_wnd(efp);
			}
			else exdata->file[0] = heading;
			return TRUE;
		}
		case idx_check::file:
		{
			decltype(exdata->file) file{};
			::strcpy_s(file, exdata->file);
			OPENFILENAMEA ofn{
				.lStructSize = sizeof(ofn),
				.hwndOwner = *exedit.hwnd_setting_dlg,
				.hInstance = nullptr,
				.lpstrFilter = efp->exfunc->get_loadable_image_extension(),
				.lpstrFile = file,
				.nMaxFile = std::size(file),
			};
			if (::GetOpenFileNameA(&ofn)) {
				efp->exfunc->set_undo(efp->processing, 0);
				std::memset(exdata->file, 0, sizeof(exdata->file));
				::strcpy_s(exdata->file, relative_path::relative{ file }.str_rel.c_str());
				exedit.update_any_exdata(efp->processing, exdata_use[idx_data::file].name);

				update_extendedfilter_wnd(efp);
			}
			return TRUE;
		}
		}
		break;
	case EXTENDEDFILTER_D_AND_D:
	{
		auto file = reinterpret_cast<char const*>(lparam);
		if (file == nullptr) break;

		efp->exfunc->set_undo(efp->processing, 0);
		std::memset(exdata->file, 0, sizeof(exdata->file));
		::strcpy_s(exdata->file, relative_path::relative{ file }.str_rel.c_str());
		exedit.update_any_exdata(efp->processing, exdata_use[idx_data::file].name);

		update_extendedfilter_wnd(efp);
		return TRUE;
	}
	}
	return FALSE;
}

int32_t func_window_init(HINSTANCE hinstance, HWND hwnd, int y, int base_id, int sw_param, ExEdit::Filter* efp)
{
	if (sw_param != 0) update_extendedfilter_wnd(efp);
	return 0;
}


////////////////////////////////
// フィルタ処理．
////////////////////////////////
constexpr int log2_max_alpha = 12, max_alpha = 1 << log2_max_alpha;

template<size_t src_step, size_t dst_step, bool antialias, bool handle_corner>
bool calc_convex_closure(i16 const* src_buf, int obj_w, int obj_h, size_t src_stride,
	i16 threshold, i16* dst_buf, size_t dst_stride, int extend, void* heap)
{
	// threshold is used as: alpha > threshold / alpha <= threshold.

	// left/right edges of non-trasparent pixels on each line.
	int const dst_w = obj_w + 2 * extend, dst_h = obj_h + 2 * extend;
	auto const
		heap1 = reinterpret_cast<int*>(heap),
		heap2 = heap1 + 2 * (dst_h + 1),
		heap3 = heap2 + 2 * (dst_h + 1),
		heap4 = heap3 + 2 * (dst_h + 1);

	// vertices of the desired convex closure,
	// which is a polygon as the number of pixels is finite.
	struct key_points {
		int top, btm;
		int* x_map;
		int* key_pts;
		int count;
		constexpr auto peek(int i) const {
			return std::pair{ key_pts[2 * (count - i)], key_pts[2 * (count - i) + 1] };
		}
		constexpr void push(int x, int y) {
			key_pts[2 * count] = x; key_pts[2 * count + 1] = y;
			count++;
		}
		constexpr void pop() { count--; }
		constexpr key_points(int top, int btm, int* x_map, int* key_pts)
			: top{ top }, btm{ btm }, x_map{ x_map }, key_pts{ key_pts }, count{ 1 } {
			key_pts[0] = x_map[top]; key_pts[1] = top;
		}
	#pragma warning ( suppress : 26495 ) // member variables intentionally left uninitialized.
		constexpr key_points() {}
	};
	key_points LT, LB, RT, RB;

	// first, traverse pixels for rough bounding.
	{
		auto const heap1r = heap1 + obj_h;
		struct bound {
			int top, btm;
			int l_min, l_min_top, l_min_btm;
			int r_max, r_max_top, r_max_btm;
		};
		auto bounds = multi_thread(obj_h, [&](int thread_id, int thread_num) -> bound {
			int top = obj_h, btm = -1,
				l_min = obj_w, l_min_top = obj_h, l_min_btm = -1,
				r_max = -1, r_max_top = obj_h, r_max_btm = -1;
			for (int y = thread_id; y < obj_h; y += thread_num) {
				int x = 0;
				for (auto line = src_buf + y * src_stride;
					x < obj_w; x++, line += src_step) {
					if (*line > threshold) goto black_found;
				}
				heap1[y] = obj_w; heap1r[y] = 0;
				continue;

			black_found:
				if (top > y) top = y;
				btm = y;

				heap1[y] = x;
				if (x <= l_min) {
					if (x < l_min) {
						l_min = x;
						l_min_top = y;
					}
					l_min_btm = y;
				}

				x = obj_w - 1;
				for (auto line = src_buf + x * src_step + y * src_stride;
					; x--, line -= src_step) {
					if (*line > threshold) break;
				}
				heap1r[y] = ~x; // "flip" so subsequent comparison will simplify.
				if (x >= r_max) {
					if (x > r_max) {
						r_max = x;
						r_max_top = y;
					}
					r_max_btm = y;
				}
			}

			return {
				top, btm,
				l_min, l_min_top, l_min_btm,
				r_max, r_max_top, r_max_btm,
			};
		});

		// combine the found boundings.
		bound bd{
			obj_h, -1,
			obj_w, obj_h, -1,
			-1, obj_h, -1,
		};
		for (auto& bd_i : bounds) {
			if (bd_i.top > bd_i.btm) continue;

			bd.top = std::min(bd.top, bd_i.top);
			bd.btm = std::max(bd.btm, bd_i.btm);

			if (bd.l_min == bd_i.l_min) {
				bd.l_min_top = std::min(bd.l_min_top, bd_i.l_min_top);
				bd.l_min_btm = std::max(bd.l_min_btm, bd_i.l_min_btm);
			}
			else if (bd.l_min > bd_i.l_min) {
				bd.l_min = bd_i.l_min;
				bd.l_min_top = bd_i.l_min_top;
				bd.l_min_btm = bd_i.l_min_btm;
			}

			if (bd.r_max == bd_i.r_max) {
				bd.r_max_top = std::min(bd.r_max_top, bd_i.r_max_top);
				bd.r_max_btm = std::max(bd.r_max_btm, bd_i.r_max_btm);
			}
			else if (bd.r_max < bd_i.r_max) {
				bd.r_max = bd_i.r_max;
				bd.r_max_top = bd_i.r_max_top;
				bd.r_max_btm = bd_i.r_max_btm;
			}
		}

		// found to be empty.
		if (bd.top > bd.btm) return false;

		// summary.
		LT = { bd.top, bd.l_min_top, heap1,  heap3 };
		LB = { bd.l_min_btm, bd.btm, heap1,  heap3 + 2 * (bd.l_min_top - bd.top + 1) };
		RT = { bd.top, bd.r_max_top, heap1r, heap4 };
		RB = { bd.r_max_btm, bd.btm, heap1r, heap4 + 2 * (bd.r_max_top - bd.top + 1) };
	}

	// identify "key points" by Graham scan (https://en.wikipedia.org/wiki/Graham_scan).
	multi_thread(2 * (LB.btm - LT.top + 1) < (1 << 6), [&](int thread_id, int thread_num) {
		// parallel loop up to four threads.
		for (int i = thread_id; i < 4; i += thread_num) {
			auto const quad = [&]{
				switch (i) {
				case 0: return &LT;
				case 1: return &LB;
				case 2: return &RT;
				case 3: return &RB;
				default: std::unreachable();
				}
			}();

			if (int const y_btm = quad->btm;
				quad->top < y_btm) {
				int const x_btm = quad->x_map[y_btm];

				auto [x1, y1] = quad->peek(1);
				int diff_x = x_btm - x1, diff_y = y_btm - y1, cmp_base = x1 * diff_y;
				int y = y1 + 1; int const* x_map = quad->x_map + y;
				for (; y < y_btm; y++, x_map++) {
					int const x = *x_map;
					cmp_base += diff_x;
					if (cmp_base > x * diff_y) {
						while (quad->count > 1) {
							auto const [x0, y0] = quad->peek(2);
							int const dx1 = x1 - x0, dy1 = y1 - y0,
								dx = x - x1, dy = y - y1;
							if (dx * dy1 > dx1 * dy) break;
							quad->pop();
							x1 = x0; y1 = y0;
						}
						quad->push(x, y);
						x1 = x; y1 = y;
						diff_x = x_btm - x; diff_y = y_btm - y; cmp_base = x * diff_y;
					}
				}
				quad->push(x_btm, y_btm);
			}
		}
	});

	// extend the polygon defined by those key points.
	if (extend > 0) {
		LT.x_map = heap1; LB.x_map = heap1 + 2 * LT.count;
		RT.x_map = heap2; RB.x_map = heap2 + 2 * RT.count;

		// suppose the two lines (y-y1)/dy_i=(x-x1)/dx_i (i=1,2) that pass the point (x1, y1).
		// move them by `length` pixels to the direction orthogonal to themselves.
		// this lambda calculates the crossing point of the moved lines with some boundary handlings.
		constexpr auto extend_point = [](int length, int x1, int y1, int dx1, int dy1, int dx2, int dy2,
			int bound, bool is_head) {
			auto const
				l1 = std::sqrtf(static_cast<float>(dx1 * dx1 + dy1 * dy1)),
				l2 = std::sqrtf(static_cast<float>(dx2 * dx2 + dy2 * dy2));
			int X1, Y1;
			if (handle_corner && (dy1 < 0 || dy2 < 0 || dx1 * dx2 < 0)) {
				// in cases where the signatures of dy1/dx1 and dy2/dx2 do not match.
				auto const t = dx1 * dy2 - dx2 * dy1;
				auto ofs_x = -(dx1 * l2 - dx2 * l1) * length / t,
					ofs_y = -(dy1 * l2 - dy2 * l1) * length / t;

				if (dy1 < 0 || dy2 < 0) {
					Y1 = y1 + static_cast<int>(std::round(ofs_y));
					if (is_head ? Y1 < bound : Y1 > bound) {
						// y-coordinate exceeds the bound.
						Y1 = bound;

						// move the point along the line to fit within the boundary.
						// is_head chooses which line to go along with.
						ofs_y -= bound - y1;
						ofs_x -= is_head ? ofs_y * dx2 / dy2 : ofs_y * dx1 / dy1;
					}
					X1 = x1 + static_cast<int>(std::round(ofs_x));
				}
				else {
					X1 = x1 + static_cast<int>(std::round(ofs_x));
					if (X1 < bound) {
						// x-coordinate exceeds the bound.
						X1 = bound;

						// move the point along the line to fit within the boundary.
						// is_head chooses which line to go along with.
						ofs_x -= bound - x1;
						ofs_y -= is_head ? ofs_x * dy2 / dx2 : ofs_x * dy1 / dx1;
					}
					Y1 = y1 + static_cast<int>(std::round(ofs_y));
				}
			}
			else {
				// dy1, dy2 >= 0 and dx1 * dx2 >= 0.
				auto const s = (dx1 * dy2 + dx2 * dy1) * length;
				auto ofs_x = -s / (dx2 * l1 + dx1 * l2), ofs_y = s / (dy2 * l1 + dy1 * l2);

				// they won't go beyond the boundary.
				X1 = x1 + static_cast<int>(std::round(ofs_x));
				Y1 = y1 + static_cast<int>(std::round(ofs_y));
			}

			return std::pair{ X1, Y1 };
		};
		multi_thread(LT.count + LB.count + RT.count + RB.count < (1 << 6), [&](int thread_id, int thread_num) {
			for (int i = thread_id; i < 4; i += thread_num) {
				auto const [quad, ext1, ext2, bd1, bd2] = [&] {
					switch (i) {
					case 0: return std::tuple{ &LT, &RT, &LB, -extend, -extend };
					case 1: return std::tuple{ &LB, &LT, &RB, -extend, obj_h + extend - 1 };
					case 2: return std::tuple{ &RT, &LT, &RB, -extend, ~(obj_w + extend - 1) };
					case 3: return std::tuple{ &RB, &RT, &LB, ~(obj_w + extend - 1), obj_h + extend - 1 };
					default: std::unreachable();
					}
				}();

				if (quad->count > 1) {
					int const* pts = quad->key_pts;
					int x1 = pts[0], y1 = pts[1]; pts += 2;

					int dx1, dy1;
					if (i % 2 == 0) {
						if (handle_corner && ext1->count > 1 && x1 == ~ext1->key_pts[0]) {
							dx1 = x1 - (~ext1->key_pts[2]);
							dy1 = y1 - ext1->key_pts[3];
						}
						else { dx1 = -1; dy1 = 0; }
					}
					else {
						if (handle_corner && ext1->count > 1 && y1 == ext1->btm) {
							dx1 = x1 - ext1->key_pts[2 * ext1->count - 4];
							dy1 = y1 - ext1->key_pts[2 * ext1->count - 3];
						}
						else { dx1 = 0; dy1 = 1; }
					}
					int* dst = quad->x_map;
					for (int j = quad->count - 1; --j >= 0; pts += 2, dst += 2) {
						int const x2 = pts[0], y2 = pts[1],
							dx2 = x2 - x1, dy2 = y2 - y1;

						std::tie(dst[0], dst[1]) = extend_point(extend, x1, y1, dx1, dy1, dx2, dy2, bd1, true);

						x1 = x2; dx1 = dx2;
						y1 = y2; dy1 = dy2;
					}
					{
						int dx2, dy2;
						if (i % 2 == 0) {
							if (handle_corner && ext2->count > 1 && y1 == ext2->top) {
								dx2 = ext2->key_pts[2] - x1;
								dy2 = ext2->key_pts[3] - y1;
							}
							else { dx2 = 0; dy2 = 1; }
						}
						else {
							if (handle_corner && ext2->count > 1 && x1 == ~ext2->key_pts[2 * ext2->count - 2]) {
								dx2 = (~ext2->key_pts[2 * ext2->count - 4]) - x1;
								dy2 = ext2->key_pts[2 * ext2->count - 3] - y1;
							}
							else { dx2 = 1; dy2 = 0; }
						}

						std::tie(dst[0], dst[1]) = extend_point(extend, x1, y1, dx1, dy1, dx2, dy2, bd2, false);
					}
				}
				else {
					quad->x_map[0] = quad->key_pts[0] - extend;
					quad->x_map[1] = quad->key_pts[1] + (i % 2 == 0 ? -extend : extend);
				}
			}
		});

		for (auto quad : { &LT, &LB, &RT, &RB }) {
			quad->key_pts = quad->x_map;
			quad->top = quad->key_pts[1];
			quad->btm = quad->key_pts[2 * quad->count - 1];
		}
		LT.x_map = LB.x_map = heap3;
		RT.x_map = RB.x_map = heap4;
	}
	else {
		// vertices dont' change. allocate the buffer for the next calculation.
		LT.x_map = LB.x_map = heap1;
		RT.x_map = RB.x_map = heap2;
	}

	// represents the area: d*y <= n*(x-1)+s, contained in the box 0 <= x,y <= 1.
	// helps drawing antialiased lines.
	struct pixel_walker {
		// assumes all of these three are positive.
		uint32_t slope_n, slope_d, state;
		bool is_next_up() const { return state > slope_d; }
		uint32_t move_to_top() {
			auto q = (state - 1) / slope_d,
				r = (state - 1) % slope_d;
			state = r + 1;
			return q;
		}
		bool adjust_fullness() {
			if (state >= slope_n + slope_d) {
				move_up();
				return true;
			}
			return false;
		}
		void move_up() { state -= slope_d; }
		void move_right() { state += slope_n; }
		i16 fill_rate() const {
			if (state >= slope_d) {
				if (state >= slope_n) {
					// 1 - 1/2 x (1-(s-n)/d) x (1-(s-d)/n) = 1 - (n+d-s)^2/(2*n*d).
					auto const a = slope_n + slope_d - state;
					return static_cast<i16>(max_alpha - (max_alpha * a * a) / (2 * slope_n * slope_d));
				}
				else {
					// 1 - 1/2 x ((1-s/n)+(1-(s-d)/n)) = (s-d/2)/n.
					return static_cast<i16>((max_alpha * (2 * state - slope_d)) / (2 * slope_n));
				}
			}
			else {
				if (state >= slope_n) {
					// 1/2 x (s/d + (s-n)/d) = (s-n/2)/d.
					return static_cast<i16>((max_alpha * (2 * state - slope_n)) / (2 * slope_d));
				}
				else {
					// 1/2 x s/d x s/n.
					return static_cast<i16>((max_alpha * (state * state)) / (2 * slope_n * slope_d));
				}
			}
		}

		pixel_walker(uint32_t n, uint32_t d) : slope_n{ n }, slope_d{ d }, state{ n } {}
		pixel_walker(int n, int d) : pixel_walker(static_cast<uint32_t>(n), static_cast<uint32_t>(d)) {}
	};
	// draw line segments surrounding those key points,
	// and at the same time, rewrite left_map and right_map so
	// they identify the range of the pixels to be filled opaque.
	multi_thread(2 * (LB.btm + 1 - LT.top) < (1 << 6), [&](int thread_id, int thread_num) {
		// parallel loop up to six threads.
		for (int i = thread_id; i < 6; i += thread_num) {
			switch (i) {
			case 0:
			{
				// initial key point.
				auto const* pts = LT.key_pts;
				int x0 = pts[0] + extend, y0 = pts[1] + extend; pts += 2;
				auto* x_map = LT.x_map + 2 * y0;
				for (int j = LT.count - 1; --j >= 0; pts += 2) {
					// find the next key point, and setup a state machine.
					int const x1 = pts[0] + extend, y1 = pts[1] + extend;
					pixel_walker pw{ x0 - x1, y1 - y0 };

					// walk through pixels while drawing lines.
					x0--;
					if constexpr (antialias) {
						for (i16* dst = dst_buf + x0 * dst_step + y0 * dst_stride;
							y0 < y1; pw.move_right(), y0++, dst += dst_stride, x_map += 2) {
							x_map[1] = x0 + 1; // beginning of "black" pixels.
							while (true) { // move horizontally.
								*dst = pw.fill_rate();
								if (!pw.is_next_up()) break;
								pw.move_up(); x0--; dst -= dst_step;
							}
							x_map[0] = x0; // end of "white" pixels + 1.
						}
					}
					else {
						for (; y0 < y1; pw.move_right(), y0++, x_map += 2) {
							if (pw.adjust_fullness()) x0--; // adjust corner case.

							// end of "white" pixels + 1 / beginning of "black" pixels.
							x_map[0] = x_map[1] = x0 + 1;

							x0 -= pw.move_to_top(); // move horizontally.
						}
					}

					// update the last key point.
					y0 = y1; x0 = x1;
				}
				break;
			}
			case 1:
			{
				// initial key point
				auto const* pts = LB.key_pts;
				int x0 = pts[0] + extend, y0 = pts[1] + extend; pts += 2;
				auto* x_map = LB.x_map + 2 * (y0 + 1);
				for (int j = LB.count - 1; --j >= 0; pts += 2) {
					// find the next key point, and setup a state machine.
					int const x1 = pts[0] + extend, y1 = pts[1] + extend;
					pixel_walker pw{ x1 - x0, y1 - y0 };

					// walk through pixels while drawing lines.
					y0++;
					if constexpr (antialias) {
						for (i16* dst = dst_buf + x0 * dst_step + y0 * dst_stride;
							y0 <= y1; pw.move_right(), y0++, dst += dst_stride, x_map += 2) {
							x_map[0] = x0; // end of "white" pixels + 1.
							while (true) { // move horizontally.
								*dst = max_alpha - pw.fill_rate();
								if (!pw.is_next_up()) break;
								pw.move_up(); x0++; dst += dst_step;
							}
							x_map[1] = x0 + 1; // beginning of "black" pixels.
						}
					}
					else {
						for (; y0 <= y1; pw.move_right(), y0++, x_map += 2) {
							x0 += pw.move_to_top(); // move horizontally.

							// end of "white" pixels + 1 / beginning of "black" pixels.
							x_map[0] = x_map[1] = x0 + 1;
						}
					}

					// update the last key point.
					y0 = y1; x0 = x1;
				}
				break;
			}
			case 2:
			{
				// initial key point.
				auto const* pts = RT.key_pts;
				int x0 = (~pts[0]) + extend, y0 = pts[1] + extend; pts += 2;
				auto* x_map = RT.x_map + 2 * y0;
				for (int j = RT.count - 1; --j >= 0; pts += 2) {
					// find the next key point, and setup a state machine.
					int const x1 = (~pts[0]) + extend, y1 = pts[1] + extend;
					pixel_walker pw{ x1 - x0, y1 - y0 };

					// walk through pixels while drawing lines.
					x0++;
					if constexpr (antialias) {
						for (i16* dst = dst_buf + x0 * dst_step + y0 * dst_stride;
							y0 < y1; pw.move_right(), y0++, dst += dst_stride, x_map += 2) {
							x_map[0] = x0; // end of "black" pixels + 1.
							while (true) { // move horizontally.
								*dst = pw.fill_rate();
								if (!pw.is_next_up()) break;
								pw.move_up(); x0++; dst += dst_step;
							}
							x_map[1] = x0 + 1; // beginning of "white" pixels.
						}
					}
					else {
						for (; y0 < y1; pw.move_right(), y0++, x_map += 2) {
							if (pw.adjust_fullness()) x0++; // adjust corner case.

							// beginning of "white" pixels / end of "black" pixels + 1.
							x_map[0] = x_map[1] = x0;

							x0 += pw.move_to_top(); // move horizontally.
						}
					}

					// update the last key point.
					y0 = y1; x0 = x1;
				}
				break;
			}
			case 3:
			{
				// initial key point
				auto const* pts = RB.key_pts;
				int x0 = (~pts[0]) + extend, y0 = pts[1] + extend; pts += 2;
				auto* x_map = RB.x_map + 2 * (y0 + 1);
				for (int j = RB.count - 1; --j >= 0; pts += 2) {
					// find the next key point, and setup a state machine.
					int const x1 = (~pts[0]) + extend, y1 = pts[1] + extend;
					pixel_walker pw{ x0 - x1, y1 - y0 };

					// walk through pixels while drawing lines.
					y0++;
					if constexpr (antialias) {
						for (i16* dst = dst_buf + x0 * dst_step + y0 * dst_stride;
							y0 <= y1; pw.move_right(), y0++, dst += dst_stride, x_map += 2) {
							x_map[1] = x0 + 1; // beginning of "white" pixels.
							while (true) { // move horizontally.
								*dst = max_alpha - pw.fill_rate();
								if (!pw.is_next_up()) break;
								pw.move_up(); x0--; dst -= dst_step;
							}
							x_map[0] = x0; // end of "black" pixels + 1.
						}
					}
					else {
						for (; y0 <= y1; pw.move_right(), y0++, x_map += 2) {
							x0 -= pw.move_to_top(); // move horizontally.

							// beginning of "white" pixels / end of "black" pixels + 1.
							x_map[0] = x_map[1] = x0;
						}
					}

					// update the last key point.
					y0 = y1; x0 = x1;
				}
				break;
			}
			case 4:
			{
				// handle pixels between y_l_top and y_l_btm.
				int const x12 = LB.key_pts[0] + extend;
				auto* x_map = LT.x_map + 2 * (LT.btm + extend);
				for (int j = LB.top - LT.btm + 1; --j >= 0; x_map += 2)
					x_map[0] = x_map[1] = x12;
				break;
			}
			case 5:
			{
				// handle pixels between y_r_top and y_r_btm.
				int const x34 = (~RB.key_pts[0]) + 1 + extend;
				auto* x_map = RT.x_map + 2 * (RT.btm + extend);
				for (int j = RB.top - RT.btm + 1; --j >= 0; x_map += 2)
					x_map[0] = x_map[1] = x34;
				break;
			}
			}
		}
	});

	// fill the rest of pixels.
	LT.top += extend; RB.btm += extend;
	multi_thread(dst_h, [&](int thread_id, int thread_num) {
		for (int y = thread_id; y < dst_h; y += thread_num) {
			i16* dst_y = dst_buf + y * dst_stride;
			if (y < LT.top || y > RB.btm) {
				for (int i = dst_w; --i >= 0; dst_y += dst_step) *dst_y = 0;
			}
			else {
				auto const l = LT.x_map + 2 * y, r = RB.x_map + 2 * y;
				int x1 = l[0], x2 = l[1], x3 = r[0], x4 = r[1];

				// white on the left side.
				for (int i = x1; --i >= 0; dst_y += dst_step) *dst_y = 0;

				// black on the middle.
				dst_y += (x2 - x1) * dst_step;
				for (int i = x3 - x2; --i >= 0; dst_y += dst_step) *dst_y = max_alpha;

				// white on the right side.
				dst_y += (x4 - x3) * dst_step;
				for (int i = dst_w - x4; --i >= 0; dst_y += dst_step) *dst_y = 0;
			}
		}
	});

	return true;
}
constexpr ExEdit::PixelYC fromRGB(uint8_t r, uint8_t g, uint8_t b) {
	// ripped a piece of code from exedit/pixel.hpp.
	auto r_ = (r << 6) + 18;
	auto g_ = (g << 6) + 18;
	auto b_ = (b << 6) + 18;
	return {
		static_cast<int16_t>(((r_* 4918)>>16)+((g_* 9655)>>16)+((b_* 1875)>>16)-3),
		static_cast<int16_t>(((r_*-2775)>>16)+((g_*-5449)>>16)+((b_* 8224)>>16)+1),
		static_cast<int16_t>(((r_* 8224)>>16)+((g_*-6887)>>16)+((b_*-1337)>>16)+1),
	};
}
BOOL func_proc(ExEdit::Filter* efp, ExEdit::FilterProcInfo* efpip)
{
	int const src_w = efpip->obj_w, src_h = efpip->obj_h;
	if (src_w <= 0 || src_h <= 0) return TRUE;

	constexpr int
		den_extend		= track_den[idx_track::extend],
		min_extend		= track_min[idx_track::extend],
		max_extend		= track_max[idx_track::extend],

		den_transp		= track_den[idx_track::transp],
		min_transp		= track_min[idx_track::transp],
		max_transp		= track_max[idx_track::transp],

		den_f_transp	= track_den[idx_track::f_transp],
		min_f_transp	= track_min[idx_track::f_transp],
		max_f_transp	= track_max[idx_track::f_transp],

		den_threshold	= track_den[idx_track::threshold],
		min_threshold	= track_min[idx_track::threshold],
		max_threshold	= track_max[idx_track::threshold],

		den_img_x		= track_den[idx_track::img_x],
		min_img_x		= track_min[idx_track::img_x],
		max_img_x		= track_max[idx_track::img_x],

		den_img_y		= track_den[idx_track::img_y],
		min_img_y		= track_min[idx_track::img_y],
		max_img_y		= track_max[idx_track::img_y];

	int const
		extend		= std::clamp(efp->track[idx_track::extend	], min_extend, std::min(max_extend,
			std::min(exedit.yca_max_w - efpip->obj_w, exedit.yca_max_h - efpip->obj_h) >> 1)),
		transp		= std::clamp(efp->track[idx_track::transp	], min_transp, max_transp),
		f_transp	= std::clamp(efp->track[idx_track::f_transp	], min_f_transp, max_f_transp),
		threshold	= std::clamp(efp->track[idx_track::threshold], min_threshold, max_threshold),
		img_x		= std::clamp(efp->track[idx_track::img_x	], min_img_x, max_img_x),
		img_y		= std::clamp(efp->track[idx_track::img_y	], min_img_y, max_img_y);
	bool const antialias = efp->check[idx_check::antialias] != check_data::unchecked;
	auto* const exdata = reinterpret_cast<Exdata*>(efp->exdata_ptr);

	int const
		alpha = std::clamp(max_alpha * (max_transp - transp) / max_transp, 0, max_alpha),
		f_alpha = std::clamp(max_alpha * (max_f_transp - f_transp) / max_f_transp, 0, max_alpha);

	int const dst_w = efpip->obj_w + 2 * extend, dst_h = efpip->obj_h + 2 * extend;

	// handle trivial cases.
	if (alpha <= 0 ||
		!(antialias ? calc_convex_closure<4, 4, true, true> : calc_convex_closure<4, 4, false, true>)
		(&efpip->obj_edit->a, efpip->obj_w, efpip->obj_h, 4 * efpip->obj_line,
			(threshold * (max_alpha - 1)) / max_threshold,
			&efpip->obj_temp->a, 4 * efpip->obj_line, extend,
			*exedit.memory_ptr)) {
		if (extend > 0) {
			auto do_work = [&]<bool handle_alpha>{
				multi_thread(efpip->obj_h, [&, src_w = efpip->obj_w](int thread_id, int thread_num) {
					for (int y = thread_id; y < dst_h; y += thread_num) {
						auto* dst_y = &efpip->obj_temp[y * efpip->obj_line];
						if (y < extend || y >= dst_h - extend) {
							for (int x = dst_w; --x >= 0; dst_y++) dst_y->a = 0;
						}
						else {
							for (int x = extend; --x >= 0; dst_y++) dst_y->a = 0;
							auto const* src_y = &efpip->obj_edit[(y - extend) * efpip->obj_line];
							if constexpr (handle_alpha) {
								for (int x = src_w; --x >= 0; dst_y++, src_y++) {
									*dst_y = *src_y;
									dst_y->a = (f_alpha * dst_y->a) >> log2_max_alpha;
								}
							}
							else {
								std::memcpy(dst_y, src_y, sizeof(*dst_y) * src_w);
								dst_y += src_w;
							}
							for (int x = extend; --x >= 0; dst_y++) dst_y->a = 0;
						}
					}
				});
			};
			if (f_alpha < max_alpha) do_work.operator()<true>(); else do_work.operator()<false>();
			std::swap(efpip->obj_edit, efpip->obj_temp);
			efpip->obj_w = dst_w; efpip->obj_h = dst_h;
		}
		else if (f_alpha < max_alpha) {
			multi_thread(dst_h, [&](int thread_id, int thread_num) {
				int y0 = dst_h * thread_id / thread_num, y1 = efpip->obj_h * (thread_id + 1) / thread_num;
				i16* dst_y = &efpip->obj_edit[y0 * efpip->obj_line].a;
				for (int y = y1 - y0; --y >= 0; dst_y += 4 * efpip->obj_line) {
					i16* dst_x = dst_y;
					for (int x = dst_w; --x >= 0; dst_x += 4)
						*dst_x = (f_alpha * (*dst_x)) >> log2_max_alpha;
				}
			});
		}
		return TRUE;
	}

	if (tiled_image img{ relative_path::absolute{exdata->file}.abs_path.c_str(), img_x, img_y, extend, efp, *exedit.memory_ptr }) {
		auto blend = [&](i16 back, ExEdit::PixelYCA const& src, int i_x, int i_y) -> ExEdit::PixelYCA {
			i16 a = (f_alpha * src.a) >> log2_max_alpha;
			if (a >= max_alpha) return src;

			i16 A = (alpha * back) >> log2_max_alpha;
			if (A <= 0) return { .y = src.y, .cb = src.cb, .cr = src.cr, .a = a };

			ExEdit::PixelYCA col = img[i_x + i_y * efpip->obj_line];
			A = (A * col.a) >> log2_max_alpha;
			if (a <= 0) return { .y = col.y, .cb = col.cb, .cr = col.cr, .a = A };

			A = ((max_alpha - a) * A) >> log2_max_alpha;
			return {
				.y  = static_cast<i16>((a * src.y  + A * col.y ) / (a + A)),
				.cb = static_cast<i16>((a * src.cb + A * col.cb) / (a + A)),
				.cr = static_cast<i16>((a * src.cr + A * col.cr) / (a + A)),
				.a  = static_cast<i16>(a + A),
			};
		};
		auto paint = [&](i16 back, int i_x, int i_y) -> ExEdit::PixelYCA {
			i16 A = (alpha * back) >> log2_max_alpha;
			if (A <= 0) return { .a = 0 };

			ExEdit::PixelYCA col = img[i_x + i_y * efpip->obj_line];
			A = (A * col.a) >> log2_max_alpha;
			return { .y = col.y, .cb = col.cb, .cr = col.cr, .a = A };
		};

		multi_thread(dst_h, [&](int thread_id, int thread_num) {
			for (int y = thread_id; y < dst_h; y += thread_num) {
				auto* dst_y = &efpip->obj_temp[y * efpip->obj_line];
				int i_y = (y + img.oy) % img.h;
				int i_x = img.ox;
				auto incr_x = [&] {i_x++; if (i_x >= img.w) i_x -= img.w; };
				if (y < extend || y >= dst_h - extend) {
					for (int x = dst_w; --x >= 0; dst_y++, incr_x())
						*dst_y = paint(dst_y->a, i_x, i_y);
				}
				else {
					for (int x = extend; --x >= 0; dst_y++, incr_x())
						*dst_y = paint(dst_y->a, i_x, i_y);

					auto* src_y = &efpip->obj_edit[(y - extend) * efpip->obj_line];
					for (int x = src_w; --x >= 0; dst_y++, incr_x(), src_y++)
						*dst_y = blend(dst_y->a, *src_y, i_x, i_y);

					for (int x = extend; --x >= 0; dst_y++, incr_x())
						*dst_y = paint(dst_y->a, i_x, i_y);
				}
			}
		});
	}
	else {
		auto const col = fromRGB(exdata->color.r, exdata->color.g, exdata->color.b);
		auto blend = [&](i16 back, ExEdit::PixelYCA const& src) -> ExEdit::PixelYCA {
			i16 a = (f_alpha * src.a) >> log2_max_alpha;
			if (a >= max_alpha) return src;

			i16 A = (alpha * back) >> log2_max_alpha;
			if (A <= 0) return { .y = src.y, .cb = src.cb, .cr = src.cr, .a = a };
			if (a <= 0) return { .y = col.y, .cb = col.cb, .cr = col.cr, .a = A };

			A = ((max_alpha - a) * A) >> log2_max_alpha;
			return {
				.y  = static_cast<i16>((a * src.y  + A * col.y ) / (a + A)),
				.cb = static_cast<i16>((a * src.cb + A * col.cb) / (a + A)),
				.cr = static_cast<i16>((a * src.cr + A * col.cr) / (a + A)),
				.a  = static_cast<i16>(a + A),
			};
		};
		auto paint = [&](i16 back) -> ExEdit::PixelYCA {
			i16 A = (alpha * back) >> log2_max_alpha;
			return { .y = col.y, .cb = col.cb, .cr = col.cr, .a = A };
		};

		multi_thread(dst_h, [&](int thread_id, int thread_num) {
			for (int y = thread_id; y < dst_h; y += thread_num) {
				auto* dst_y = &efpip->obj_temp[y * efpip->obj_line];
				if (y < extend || y >= dst_h - extend) {
					for (int x = dst_w; --x >= 0; dst_y++)
						*dst_y = paint(dst_y->a);
				}
				else {
					for (int x = extend; --x >= 0; dst_y++)
						*dst_y = paint(dst_y->a);

					auto* src_y = &efpip->obj_edit[(y - extend) * efpip->obj_line];
					for (int x = src_w; --x >= 0; dst_y++, src_y++)
						*dst_y = blend(dst_y->a, *src_y);

					for (int x = extend; --x >= 0; dst_y++)
						*dst_y = paint(dst_y->a);
				}
			}
		});
	}
	std::swap(efpip->obj_edit, efpip->obj_temp);
	efpip->obj_w += 2 * extend;
	efpip->obj_h += 2 * extend;
	return TRUE;
}


////////////////////////////////
// DLL 初期化．
////////////////////////////////
BOOL WINAPI DllMain(HINSTANCE hinst, DWORD fdwReason, LPVOID lpvReserved)
{
	switch (fdwReason) {
	case DLL_PROCESS_ATTACH:
		::DisableThreadLibraryCalls(hinst);
		break;
	}
	return TRUE;
}


////////////////////////////////
// エントリポイント．
////////////////////////////////
EXTERN_C __declspec(dllexport) ExEdit::Filter* const* __stdcall GetFilterTableList() {
	constexpr static ExEdit::Filter* filter_list[] = {
		&filter,
		nullptr,
	};

	return filter_list;
}

