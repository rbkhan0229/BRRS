#!/usr/bin/env python3
"""Static scientific plot of measured network PER; no interpolation claims."""
import json
import os
from pathlib import Path
R=Path(__file__).resolve().parent
os.environ.setdefault('MPLCONFIGDIR',str(R/'.matplotlib'))
os.environ.setdefault('XDG_CACHE_HOME',str(R/'.cache'))
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.ticker import MultipleLocator

records=json.loads((R/'rotation_observations.json').read_text())
fig,axes=plt.subplots(2,1,figsize=(10,7),sharex=True,layout='constrained')
for pac,color,marker in [(8,'#1859a9','o'),(4,'#c04d13','s')]:
    data=sorted((a['conditions']['lead_us'],a['worst_node_per_percent']) for a in records.values()
                if a['conditions']['sensors']==6 and a['conditions']['rx_pac']==pac)
    x,y=zip(*data)
    for ax in axes:
        ax.plot(x,y,color=color,marker=marker,ls='--',lw=1,ms=5,label=f'PAC{pac}')
        ax.axhline(1,color='#333333',lw=1,ls=':')
axes[0].set_ylim(-1,80);axes[0].set_ylabel('Worst physical-node PER (%)')
axes[0].set_title('M32 / 6 TX / 13 slots / fixed block2 mapping\n1,000 superframes/point; cable-obstruction timing unknown',loc='left',fontsize=12)
axes[0].legend(loc='upper right')
axes[1].set_ylim(-.1,5);axes[1].set_ylabel('Same data, 0–5% view');axes[1].set_xlabel('RX lead before expected preamble start (µs)')
axes[1].text(.98,.97,'Goal: every physical node PER < 1%',ha='right',va='top',transform=axes[1].transAxes,fontsize=10)
for ax in axes:
    ax.grid(alpha=.2);ax.spines[['top','right']].set_visible(False)
    ax.xaxis.set_major_locator(MultipleLocator(2));ax.xaxis.set_minor_locator(MultipleLocator(1))
fig.suptitle('Measured points only; dashed lines guide the eye. Run order/time retained in the data.',fontsize=9)
fig.savefig(R/'network_lead_per.png',dpi=180)
fig.savefig(R/'network_lead_per.pdf')
print(R/'network_lead_per.png')
