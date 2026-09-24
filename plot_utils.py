"""
Author: Matteo Loporchio
"""

import matplotlib.pyplot as plt
import seaborn as sns
import os
from matplotlib import font_manager
from pathlib import Path

# SOURCE: https://colorbrewer2.org/#type=qualitative&scheme=Set1&n=5
COLORS = ['#e41a1c','#377eb8','#4daf4a','#984ea3','#ff7f00','#f781bf',"#4d4d4d"]
DEFAULT_FONT_SIZE = 18
DEFAULT_FIGURE_SIZE = (3, 3)

# Set default font
DEFAULT_FONT_PATH = 'fonts/texgyreheros-regular.otf'
if os.path.exists(DEFAULT_FONT_PATH):
    font_manager.fontManager.addfont(DEFAULT_FONT_PATH)
    prop = font_manager.FontProperties(fname=DEFAULT_FONT_PATH)
    plt.rcParams['font.family'] = 'sans-serif'
    plt.rcParams['font.sans-serif'] = prop.get_name()

plt.rcParams.update({'font.size': DEFAULT_FONT_SIZE})
plt.rcParams.update({'axes.titlesize': DEFAULT_FONT_SIZE})