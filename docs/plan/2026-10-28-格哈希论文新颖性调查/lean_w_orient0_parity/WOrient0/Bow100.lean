import WOrient0.Parity

/-!
# 弯 100 px 装置的全部小格与两个探针（由 w_orient0_explainer.html 的几何代码导出）

装置：H、V 都是幅 100 px 的正弦折线（24 段），O 取沿 H 的第 1 个交点 (-92.2, -92.2)，
orient0 = 1，w = -1。小格编号 id = (i + 1)·26 + (j + 1)，i、j ∈ −1..24，
其中 −1 是首端外延、24 是末端外延；外延小格的 orient 与贴着的面内小格相同。
探针 A₁ = (0, 80) 点中 3 个小格，A₂ = (60, 80) 点中 5 个。
Lean 核对的是：这些数据满足守恒律 Σ orient = w，以及由此得到的计数。
「这些小格确实是 A₁、A₂ 的原像」是几何事实，由页面代码算出，不在 Lean 里。
-/

namespace WOrient0.Bow100

def cells : List Cell := [
  ⟨0, -1, false⟩, ⟨1, -1, false⟩, ⟨2, -1, false⟩, ⟨3, -1, false⟩, ⟨4, -1, false⟩, ⟨5, -1, false⟩, ⟨6, -1, false⟩, ⟨7, 1, false⟩, ⟨8, 1, false⟩, ⟨9, 1, false⟩, ⟨10, 1, false⟩, ⟨11, 1, false⟩, ⟨12, 1, false⟩,
  ⟨13, 1, false⟩, ⟨14, 1, false⟩, ⟨15, 1, false⟩, ⟨16, 1, false⟩, ⟨17, 1, false⟩, ⟨18, 1, false⟩, ⟨19, -1, false⟩, ⟨20, -1, false⟩, ⟨21, -1, false⟩, ⟨22, -1, false⟩, ⟨23, -1, false⟩, ⟨24, -1, false⟩, ⟨25, -1, false⟩,
  ⟨26, -1, false⟩, ⟨27, -1, true⟩, ⟨28, -1, true⟩, ⟨29, -1, true⟩, ⟨30, -1, true⟩, ⟨31, -1, true⟩, ⟨32, -1, true⟩, ⟨33, 1, true⟩, ⟨34, 1, true⟩, ⟨35, 1, true⟩, ⟨36, 1, true⟩, ⟨37, 1, true⟩, ⟨38, 1, true⟩,
  ⟨39, 1, true⟩, ⟨40, 1, true⟩, ⟨41, 1, true⟩, ⟨42, 1, true⟩, ⟨43, 1, true⟩, ⟨44, 1, true⟩, ⟨45, -1, true⟩, ⟨46, -1, true⟩, ⟨47, -1, true⟩, ⟨48, -1, true⟩, ⟨49, -1, true⟩, ⟨50, -1, true⟩, ⟨51, -1, false⟩,
  ⟨52, -1, false⟩, ⟨53, -1, true⟩, ⟨54, -1, true⟩, ⟨55, -1, true⟩, ⟨56, -1, true⟩, ⟨57, -1, true⟩, ⟨58, -1, true⟩, ⟨59, -1, true⟩, ⟨60, 1, true⟩, ⟨61, 1, true⟩, ⟨62, 1, true⟩, ⟨63, 1, true⟩, ⟨64, 1, true⟩,
  ⟨65, 1, true⟩, ⟨66, 1, true⟩, ⟨67, 1, true⟩, ⟨68, 1, true⟩, ⟨69, 1, true⟩, ⟨70, -1, true⟩, ⟨71, -1, true⟩, ⟨72, -1, true⟩, ⟨73, -1, true⟩, ⟨74, -1, true⟩, ⟨75, -1, true⟩, ⟨76, -1, true⟩, ⟨77, -1, false⟩,
  ⟨78, -1, false⟩, ⟨79, -1, true⟩, ⟨80, -1, true⟩, ⟨81, -1, true⟩, ⟨82, -1, true⟩, ⟨83, -1, true⟩, ⟨84, -1, true⟩, ⟨85, -1, true⟩, ⟨86, 1, true⟩, ⟨87, 1, true⟩, ⟨88, 1, true⟩, ⟨89, 1, true⟩, ⟨90, 1, true⟩,
  ⟨91, 1, true⟩, ⟨92, 1, true⟩, ⟨93, 1, true⟩, ⟨94, 1, true⟩, ⟨95, 1, true⟩, ⟨96, -1, true⟩, ⟨97, -1, true⟩, ⟨98, -1, true⟩, ⟨99, -1, true⟩, ⟨100, -1, true⟩, ⟨101, -1, true⟩, ⟨102, -1, true⟩, ⟨103, -1, false⟩,
  ⟨104, -1, false⟩, ⟨105, -1, true⟩, ⟨106, -1, true⟩, ⟨107, -1, true⟩, ⟨108, -1, true⟩, ⟨109, -1, true⟩, ⟨110, -1, true⟩, ⟨111, -1, true⟩, ⟨112, 1, true⟩, ⟨113, 1, true⟩, ⟨114, 1, true⟩, ⟨115, 1, true⟩, ⟨116, 1, true⟩,
  ⟨117, 1, true⟩, ⟨118, 1, true⟩, ⟨119, 1, true⟩, ⟨120, 1, true⟩, ⟨121, 1, true⟩, ⟨122, -1, true⟩, ⟨123, -1, true⟩, ⟨124, -1, true⟩, ⟨125, -1, true⟩, ⟨126, -1, true⟩, ⟨127, -1, true⟩, ⟨128, -1, true⟩, ⟨129, -1, false⟩,
  ⟨130, -1, false⟩, ⟨131, -1, true⟩, ⟨132, -1, true⟩, ⟨133, -1, true⟩, ⟨134, -1, true⟩, ⟨135, -1, true⟩, ⟨136, -1, true⟩, ⟨137, -1, true⟩, ⟨138, 1, true⟩, ⟨139, 1, true⟩, ⟨140, 1, true⟩, ⟨141, 1, true⟩, ⟨142, 1, true⟩,
  ⟨143, 1, true⟩, ⟨144, 1, true⟩, ⟨145, 1, true⟩, ⟨146, 1, true⟩, ⟨147, 1, true⟩, ⟨148, -1, true⟩, ⟨149, -1, true⟩, ⟨150, -1, true⟩, ⟨151, -1, true⟩, ⟨152, -1, true⟩, ⟨153, -1, true⟩, ⟨154, -1, true⟩, ⟨155, -1, false⟩,
  ⟨156, -1, false⟩, ⟨157, -1, true⟩, ⟨158, -1, true⟩, ⟨159, -1, true⟩, ⟨160, -1, true⟩, ⟨161, -1, true⟩, ⟨162, -1, true⟩, ⟨163, 1, true⟩, ⟨164, 1, true⟩, ⟨165, 1, true⟩, ⟨166, 1, true⟩, ⟨167, 1, true⟩, ⟨168, 1, true⟩,
  ⟨169, 1, true⟩, ⟨170, 1, true⟩, ⟨171, 1, true⟩, ⟨172, 1, true⟩, ⟨173, 1, true⟩, ⟨174, 1, true⟩, ⟨175, -1, true⟩, ⟨176, -1, true⟩, ⟨177, -1, true⟩, ⟨178, -1, true⟩, ⟨179, -1, true⟩, ⟨180, -1, true⟩, ⟨181, -1, false⟩,
  ⟨182, 1, false⟩, ⟨183, 1, true⟩, ⟨184, -1, true⟩, ⟨185, -1, true⟩, ⟨186, -1, true⟩, ⟨187, -1, true⟩, ⟨188, 1, true⟩, ⟨189, 1, true⟩, ⟨190, 1, true⟩, ⟨191, 1, true⟩, ⟨192, 1, true⟩, ⟨193, 1, true⟩, ⟨194, 1, true⟩,
  ⟨195, 1, true⟩, ⟨196, 1, true⟩, ⟨197, 1, true⟩, ⟨198, 1, true⟩, ⟨199, 1, true⟩, ⟨200, 1, true⟩, ⟨201, 1, true⟩, ⟨202, -1, true⟩, ⟨203, -1, true⟩, ⟨204, -1, true⟩, ⟨205, -1, true⟩, ⟨206, 1, true⟩, ⟨207, 1, false⟩,
  ⟨208, 1, false⟩, ⟨209, 1, true⟩, ⟨210, 1, true⟩, ⟨211, 1, true⟩, ⟨212, 1, true⟩, ⟨213, 1, true⟩, ⟨214, 1, true⟩, ⟨215, 1, true⟩, ⟨216, 1, true⟩, ⟨217, 1, true⟩, ⟨218, 1, true⟩, ⟨219, 1, true⟩, ⟨220, 1, true⟩,
  ⟨221, 1, true⟩, ⟨222, 1, true⟩, ⟨223, 1, true⟩, ⟨224, 1, true⟩, ⟨225, 1, true⟩, ⟨226, 1, true⟩, ⟨227, 1, true⟩, ⟨228, 1, true⟩, ⟨229, 1, true⟩, ⟨230, 1, true⟩, ⟨231, 1, true⟩, ⟨232, 1, true⟩, ⟨233, 1, false⟩,
  ⟨234, 1, false⟩, ⟨235, 1, true⟩, ⟨236, 1, true⟩, ⟨237, 1, true⟩, ⟨238, 1, true⟩, ⟨239, 1, true⟩, ⟨240, 1, true⟩, ⟨241, 1, true⟩, ⟨242, 1, true⟩, ⟨243, 1, true⟩, ⟨244, 1, true⟩, ⟨245, -1, true⟩, ⟨246, -1, true⟩,
  ⟨247, -1, true⟩, ⟨248, -1, true⟩, ⟨249, 1, true⟩, ⟨250, 1, true⟩, ⟨251, 1, true⟩, ⟨252, 1, true⟩, ⟨253, 1, true⟩, ⟨254, 1, true⟩, ⟨255, 1, true⟩, ⟨256, 1, true⟩, ⟨257, 1, true⟩, ⟨258, 1, true⟩, ⟨259, 1, false⟩,
  ⟨260, 1, false⟩, ⟨261, 1, true⟩, ⟨262, 1, true⟩, ⟨263, 1, true⟩, ⟨264, 1, true⟩, ⟨265, 1, true⟩, ⟨266, 1, true⟩, ⟨267, 1, true⟩, ⟨268, 1, true⟩, ⟨269, 1, true⟩, ⟨270, -1, true⟩, ⟨271, -1, true⟩, ⟨272, -1, true⟩,
  ⟨273, -1, true⟩, ⟨274, -1, true⟩, ⟨275, -1, true⟩, ⟨276, 1, true⟩, ⟨277, 1, true⟩, ⟨278, 1, true⟩, ⟨279, 1, true⟩, ⟨280, 1, true⟩, ⟨281, 1, true⟩, ⟨282, 1, true⟩, ⟨283, 1, true⟩, ⟨284, 1, true⟩, ⟨285, 1, false⟩,
  ⟨286, 1, false⟩, ⟨287, 1, true⟩, ⟨288, 1, true⟩, ⟨289, 1, true⟩, ⟨290, 1, true⟩, ⟨291, 1, true⟩, ⟨292, 1, true⟩, ⟨293, 1, true⟩, ⟨294, 1, true⟩, ⟨295, -1, true⟩, ⟨296, -1, true⟩, ⟨297, -1, true⟩, ⟨298, -1, true⟩,
  ⟨299, -1, true⟩, ⟨300, -1, true⟩, ⟨301, -1, true⟩, ⟨302, -1, true⟩, ⟨303, 1, true⟩, ⟨304, 1, true⟩, ⟨305, 1, true⟩, ⟨306, 1, true⟩, ⟨307, 1, true⟩, ⟨308, 1, true⟩, ⟨309, 1, true⟩, ⟨310, 1, true⟩, ⟨311, 1, false⟩,
  ⟨312, 1, false⟩, ⟨313, 1, true⟩, ⟨314, 1, true⟩, ⟨315, 1, true⟩, ⟨316, 1, true⟩, ⟨317, 1, true⟩, ⟨318, 1, true⟩, ⟨319, 1, true⟩, ⟨320, 1, true⟩, ⟨321, -1, true⟩, ⟨322, -1, true⟩, ⟨323, -1, true⟩, ⟨324, -1, true⟩,
  ⟨325, -1, true⟩, ⟨326, -1, true⟩, ⟨327, -1, true⟩, ⟨328, -1, true⟩, ⟨329, 1, true⟩, ⟨330, 1, true⟩, ⟨331, 1, true⟩, ⟨332, 1, true⟩, ⟨333, 1, true⟩, ⟨334, 1, true⟩, ⟨335, 1, true⟩, ⟨336, 1, true⟩, ⟨337, 1, false⟩,
  ⟨338, 1, false⟩, ⟨339, 1, true⟩, ⟨340, 1, true⟩, ⟨341, 1, true⟩, ⟨342, 1, true⟩, ⟨343, 1, true⟩, ⟨344, 1, true⟩, ⟨345, 1, true⟩, ⟨346, 1, true⟩, ⟨347, -1, true⟩, ⟨348, -1, true⟩, ⟨349, -1, true⟩, ⟨350, -1, true⟩,
  ⟨351, -1, true⟩, ⟨352, -1, true⟩, ⟨353, -1, true⟩, ⟨354, -1, true⟩, ⟨355, 1, true⟩, ⟨356, 1, true⟩, ⟨357, 1, true⟩, ⟨358, 1, true⟩, ⟨359, 1, true⟩, ⟨360, 1, true⟩, ⟨361, 1, true⟩, ⟨362, 1, true⟩, ⟨363, 1, false⟩,
  ⟨364, 1, false⟩, ⟨365, 1, true⟩, ⟨366, 1, true⟩, ⟨367, 1, true⟩, ⟨368, 1, true⟩, ⟨369, 1, true⟩, ⟨370, 1, true⟩, ⟨371, 1, true⟩, ⟨372, 1, true⟩, ⟨373, -1, true⟩, ⟨374, -1, true⟩, ⟨375, -1, true⟩, ⟨376, -1, true⟩,
  ⟨377, -1, true⟩, ⟨378, -1, true⟩, ⟨379, -1, true⟩, ⟨380, -1, true⟩, ⟨381, 1, true⟩, ⟨382, 1, true⟩, ⟨383, 1, true⟩, ⟨384, 1, true⟩, ⟨385, 1, true⟩, ⟨386, 1, true⟩, ⟨387, 1, true⟩, ⟨388, 1, true⟩, ⟨389, 1, false⟩,
  ⟨390, 1, false⟩, ⟨391, 1, true⟩, ⟨392, 1, true⟩, ⟨393, 1, true⟩, ⟨394, 1, true⟩, ⟨395, 1, true⟩, ⟨396, 1, true⟩, ⟨397, 1, true⟩, ⟨398, 1, true⟩, ⟨399, 1, true⟩, ⟨400, -1, true⟩, ⟨401, -1, true⟩, ⟨402, -1, true⟩,
  ⟨403, -1, true⟩, ⟨404, -1, true⟩, ⟨405, -1, true⟩, ⟨406, 1, true⟩, ⟨407, 1, true⟩, ⟨408, 1, true⟩, ⟨409, 1, true⟩, ⟨410, 1, true⟩, ⟨411, 1, true⟩, ⟨412, 1, true⟩, ⟨413, 1, true⟩, ⟨414, 1, true⟩, ⟨415, 1, false⟩,
  ⟨416, 1, false⟩, ⟨417, 1, true⟩, ⟨418, 1, true⟩, ⟨419, 1, true⟩, ⟨420, 1, true⟩, ⟨421, 1, true⟩, ⟨422, 1, true⟩, ⟨423, 1, true⟩, ⟨424, 1, true⟩, ⟨425, 1, true⟩, ⟨426, 1, true⟩, ⟨427, -1, true⟩, ⟨428, -1, true⟩,
  ⟨429, -1, true⟩, ⟨430, -1, true⟩, ⟨431, 1, true⟩, ⟨432, 1, true⟩, ⟨433, 1, true⟩, ⟨434, 1, true⟩, ⟨435, 1, true⟩, ⟨436, 1, true⟩, ⟨437, 1, true⟩, ⟨438, 1, true⟩, ⟨439, 1, true⟩, ⟨440, 1, true⟩, ⟨441, 1, false⟩,
  ⟨442, 1, false⟩, ⟨443, 1, true⟩, ⟨444, 1, true⟩, ⟨445, 1, true⟩, ⟨446, 1, true⟩, ⟨447, 1, true⟩, ⟨448, 1, true⟩, ⟨449, 1, true⟩, ⟨450, 1, true⟩, ⟨451, 1, true⟩, ⟨452, 1, true⟩, ⟨453, 1, true⟩, ⟨454, 1, true⟩,
  ⟨455, 1, true⟩, ⟨456, 1, true⟩, ⟨457, 1, true⟩, ⟨458, 1, true⟩, ⟨459, 1, true⟩, ⟨460, 1, true⟩, ⟨461, 1, true⟩, ⟨462, 1, true⟩, ⟨463, 1, true⟩, ⟨464, 1, true⟩, ⟨465, 1, true⟩, ⟨466, 1, true⟩, ⟨467, 1, false⟩,
  ⟨468, 1, false⟩, ⟨469, 1, true⟩, ⟨470, -1, true⟩, ⟨471, -1, true⟩, ⟨472, -1, true⟩, ⟨473, -1, true⟩, ⟨474, 1, true⟩, ⟨475, 1, true⟩, ⟨476, 1, true⟩, ⟨477, 1, true⟩, ⟨478, 1, true⟩, ⟨479, 1, true⟩, ⟨480, 1, true⟩,
  ⟨481, 1, true⟩, ⟨482, 1, true⟩, ⟨483, 1, true⟩, ⟨484, 1, true⟩, ⟨485, 1, true⟩, ⟨486, 1, true⟩, ⟨487, 1, true⟩, ⟨488, -1, true⟩, ⟨489, -1, true⟩, ⟨490, -1, true⟩, ⟨491, -1, true⟩, ⟨492, 1, true⟩, ⟨493, 1, false⟩,
  ⟨494, -1, false⟩, ⟨495, -1, true⟩, ⟨496, -1, true⟩, ⟨497, -1, true⟩, ⟨498, -1, true⟩, ⟨499, -1, true⟩, ⟨500, -1, true⟩, ⟨501, 1, true⟩, ⟨502, 1, true⟩, ⟨503, 1, true⟩, ⟨504, 1, true⟩, ⟨505, 1, true⟩, ⟨506, 1, true⟩,
  ⟨507, 1, true⟩, ⟨508, 1, true⟩, ⟨509, 1, true⟩, ⟨510, 1, true⟩, ⟨511, 1, true⟩, ⟨512, 1, true⟩, ⟨513, -1, true⟩, ⟨514, -1, true⟩, ⟨515, -1, true⟩, ⟨516, -1, true⟩, ⟨517, -1, true⟩, ⟨518, -1, true⟩, ⟨519, -1, false⟩,
  ⟨520, -1, false⟩, ⟨521, -1, true⟩, ⟨522, -1, true⟩, ⟨523, -1, true⟩, ⟨524, -1, true⟩, ⟨525, -1, true⟩, ⟨526, -1, true⟩, ⟨527, -1, true⟩, ⟨528, 1, true⟩, ⟨529, 1, true⟩, ⟨530, 1, true⟩, ⟨531, 1, true⟩, ⟨532, 1, true⟩,
  ⟨533, 1, true⟩, ⟨534, 1, true⟩, ⟨535, 1, true⟩, ⟨536, 1, true⟩, ⟨537, 1, true⟩, ⟨538, -1, true⟩, ⟨539, -1, true⟩, ⟨540, -1, true⟩, ⟨541, -1, true⟩, ⟨542, -1, true⟩, ⟨543, -1, true⟩, ⟨544, -1, true⟩, ⟨545, -1, false⟩,
  ⟨546, -1, false⟩, ⟨547, -1, true⟩, ⟨548, -1, true⟩, ⟨549, -1, true⟩, ⟨550, -1, true⟩, ⟨551, -1, true⟩, ⟨552, -1, true⟩, ⟨553, -1, true⟩, ⟨554, 1, true⟩, ⟨555, 1, true⟩, ⟨556, 1, true⟩, ⟨557, 1, true⟩, ⟨558, 1, true⟩,
  ⟨559, 1, true⟩, ⟨560, 1, true⟩, ⟨561, 1, true⟩, ⟨562, 1, true⟩, ⟨563, 1, true⟩, ⟨564, -1, true⟩, ⟨565, -1, true⟩, ⟨566, -1, true⟩, ⟨567, -1, true⟩, ⟨568, -1, true⟩, ⟨569, -1, true⟩, ⟨570, -1, true⟩, ⟨571, -1, false⟩,
  ⟨572, -1, false⟩, ⟨573, -1, true⟩, ⟨574, -1, true⟩, ⟨575, -1, true⟩, ⟨576, -1, true⟩, ⟨577, -1, true⟩, ⟨578, -1, true⟩, ⟨579, -1, true⟩, ⟨580, 1, true⟩, ⟨581, 1, true⟩, ⟨582, 1, true⟩, ⟨583, 1, true⟩, ⟨584, 1, true⟩,
  ⟨585, 1, true⟩, ⟨586, 1, true⟩, ⟨587, 1, true⟩, ⟨588, 1, true⟩, ⟨589, 1, true⟩, ⟨590, -1, true⟩, ⟨591, -1, true⟩, ⟨592, -1, true⟩, ⟨593, -1, true⟩, ⟨594, -1, true⟩, ⟨595, -1, true⟩, ⟨596, -1, true⟩, ⟨597, -1, false⟩,
  ⟨598, -1, false⟩, ⟨599, -1, true⟩, ⟨600, -1, true⟩, ⟨601, -1, true⟩, ⟨602, -1, true⟩, ⟨603, -1, true⟩, ⟨604, -1, true⟩, ⟨605, -1, true⟩, ⟨606, 1, true⟩, ⟨607, 1, true⟩, ⟨608, 1, true⟩, ⟨609, 1, true⟩, ⟨610, 1, true⟩,
  ⟨611, 1, true⟩, ⟨612, 1, true⟩, ⟨613, 1, true⟩, ⟨614, 1, true⟩, ⟨615, 1, true⟩, ⟨616, -1, true⟩, ⟨617, -1, true⟩, ⟨618, -1, true⟩, ⟨619, -1, true⟩, ⟨620, -1, true⟩, ⟨621, -1, true⟩, ⟨622, -1, true⟩, ⟨623, -1, false⟩,
  ⟨624, -1, false⟩, ⟨625, -1, true⟩, ⟨626, -1, true⟩, ⟨627, -1, true⟩, ⟨628, -1, true⟩, ⟨629, -1, true⟩, ⟨630, -1, true⟩, ⟨631, 1, true⟩, ⟨632, 1, true⟩, ⟨633, 1, true⟩, ⟨634, 1, true⟩, ⟨635, 1, true⟩, ⟨636, 1, true⟩,
  ⟨637, 1, true⟩, ⟨638, 1, true⟩, ⟨639, 1, true⟩, ⟨640, 1, true⟩, ⟨641, 1, true⟩, ⟨642, 1, true⟩, ⟨643, -1, true⟩, ⟨644, -1, true⟩, ⟨645, -1, true⟩, ⟨646, -1, true⟩, ⟨647, -1, true⟩, ⟨648, -1, true⟩, ⟨649, -1, false⟩,
  ⟨650, -1, false⟩, ⟨651, -1, false⟩, ⟨652, -1, false⟩, ⟨653, -1, false⟩, ⟨654, -1, false⟩, ⟨655, -1, false⟩, ⟨656, -1, false⟩, ⟨657, 1, false⟩, ⟨658, 1, false⟩, ⟨659, 1, false⟩, ⟨660, 1, false⟩, ⟨661, 1, false⟩, ⟨662, 1, false⟩,
  ⟨663, 1, false⟩, ⟨664, 1, false⟩, ⟨665, 1, false⟩, ⟨666, 1, false⟩, ⟨667, 1, false⟩, ⟨668, 1, false⟩, ⟨669, -1, false⟩, ⟨670, -1, false⟩, ⟨671, -1, false⟩, ⟨672, -1, false⟩, ⟨673, -1, false⟩, ⟨674, -1, false⟩, ⟨675, -1, false⟩
]

def rig : Rig where
  cells := cells
  orient0 := 1
  w := -1
  orient_pm := by decide +kernel
  orient0_pm := by decide +kernel
  w_range := by decide +kernel

/-- A₁ = (0, 80) 点中的小格编号。 -/
def hits1 : List Nat := [0, 118, 675]
/-- A₂ = (60, 80) 点中的小格编号。 -/
def hits2 : List Nat := [0, 145, 349, 422, 675]

def probe1 : Probe rig := ⟨fun c => hits1.contains c.id, by decide +kernel⟩
def probe2 : Probe rig := ⟨fun c => hits2.contains c.id, by decide +kernel⟩

theorem census1 : (probe1.fIn, probe1.bIn, probe1.fExt, probe1.bExt) = (1, 0, 0, 2) := by decide +kernel
theorem census2 : (probe2.fIn, probe2.bIn, probe2.fExt, probe2.bExt) = (2, 1, 0, 2) := by decide +kernel

/-- 同一副轴线、同样的外延计数（0 正 2 背），面内正面数 1 与 2 一奇一偶。 -/
theorem fIn_parity_not_determined :
    probe1.fExt = probe2.fExt ∧ probe1.bExt = probe2.bExt ∧ probe1.fIn % 2 ≠ probe2.fIn % 2 := by
  decide +kernel

/-- 含外延的 (f, b, n)：A₁ 是 (1, 2, 3)，A₂ 是 (2, 3, 5)；f − b 都是 −1 = w·orient0。 -/
theorem census1_total : (probe1.f, probe1.b, probe1.n) = (1, 2, 3) := by decide +kernel
theorem census2_total : (probe2.f, probe2.b, probe2.n) = (2, 3, 5) := by decide +kernel
/-- 含外延的正面数 f 也一奇一偶。 -/
theorem f_parity_not_determined : probe1.f % 2 ≠ probe2.f % 2 := by decide +kernel

/-- 面内原像个数的奇偶与命题 P2 一致：1 ≡ −1 + 2，3 ≡ −1 + 2 (mod 2)。 -/
example : (probe1.nIn : Int) % 2 = (rig.w * rig.orient0 + (probe1.nExt : Int)) % 2 :=
  probe1.inner_parity
example : (probe2.nIn : Int) % 2 = (rig.w * rig.orient0 + (probe2.nExt : Int)) % 2 :=
  probe2.inner_parity

/-- 小格表的编码良构：编号恰是 0..675 无重无漏，inner 标志与行列范围一致（i = id / 26，j = id % 26，
面内是 1 ≤ i ≤ 24 且 1 ≤ j ≤ 24）。 -/
theorem cells_grid_wf :
    cells.map Cell.id = List.range 676 ∧
    cells.all (fun c =>
      let i := c.id / 26
      let j := c.id % 26
      c.inner == (0 < i && i < 25 && 0 < j && j < 25)) = true := by
  decide +kernel

end WOrient0.Bow100
