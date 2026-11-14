"""Static C08/PLL/target guard audit, separate from clock-manager execution."""
import hashlib
import re
import subprocess


def audit(root, pin):
    sources, checks = {}, []

    def source(path):
        text = (root / path).read_text()
        assert subprocess.check_output(['git', 'show', f'{pin}:{path}'], cwd=root) == text.encode()
        sources[path] = hashlib.sha256(text.encode()).hexdigest()
        return re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"', '', text, flags=re.S)

    def function(path, name):
        text = source(path)
        match = re.search(r'\b' + name + r'\s*\([^;{}]*\)\s*\{', text)
        assert match, name
        start = pos = match.end()
        depth = 1
        while depth:
            depth += (text[pos] == '{') - (text[pos] == '}')
            pos += 1
        return re.sub(r'\s+', '', text[start:pos-1])

    def ordered(body, *parts):
        pos = 0
        for part in parts:
            at = body.find(part, pos)
            assert at >= 0, part
            pos = at + len(part)
        checks.append(list(parts))

    path = 'drivers/gpu/drm/msm/dsi-staging/dsi_display.c'
    assert function(path, 'dsi_display_wake_up') == 'return0;'
    checks.append(['pinned wake_up is a no-op'])
    ordered(function(path, 'dsi_host_transfer'),
            'dsi_display_clk_ctrl(display->dsi_clk_handle,DSI_ALL_CLKS,DSI_CLK_ON)',
            'dsi_display_wake_up(display)', 'dsi_display_cmd_engine_enable(display)',
            'if(display->tx_cmd_buf==NULL)', 'dsi_ctrl_cmd_transfer(',
            'dsi_display_cmd_engine_disable(display)',
            'dsi_display_clk_ctrl(display->dsi_clk_handle,DSI_ALL_CLKS,DSI_CLK_OFF)')
    ordered(function(path, 'dsi_display_prepare'),
            'dsi_display_clk_ctrl(display->dsi_clk_handle,DSI_CORE_CLK,DSI_CLK_ON)',
            'dsi_display_clk_ctrl(display->dsi_clk_handle,DSI_LINK_CLK,DSI_CLK_ON)',
            'dsi_panel_prepare(display->panel)')
    assert 'if(display->config.panel_mode==DSI_OP_CMD_MODE)dsi_display_clk_ctrl(display->dsi_clk_handle,DSI_ALL_CLKS,DSI_CLK_OFF)' in function(path, 'dsi_display_post_enable')
    ordered(function(path, 'dsi_display_pre_disable'),
            'if(display->config.panel_mode==DSI_OP_CMD_MODE)dsi_display_clk_ctrl(display->dsi_clk_handle,DSI_ALL_CLKS,DSI_CLK_ON)',
            'dsi_panel_pre_disable(display->panel)')
    ordered(function(path, 'dsi_display_unprepare'),
            'dsi_panel_unprepare(display->panel)', 'dsi_display_ctrl_host_disable(display)',
            'dsi_display_clk_ctrl(display->dsi_clk_handle,DSI_LINK_CLK,DSI_CLK_OFF)',
            'dsi_display_clk_ctrl(display->dsi_clk_handle,DSI_CORE_CLK,DSI_CLK_OFF)',
            'dsi_panel_post_unprepare(display->panel)')
    ordered(function(path, 'dsi_display_pre_kickoff'),
            'if(atomic_read(&display->clkrate_change_pending))',
            'dsi_panel_acquire_panel_lock(display->panel)',
            'dsi_ctrl_wait_for_cmd_mode_mdp_idle(ctrl)', 'dsi_display_force_update_dsi_clk(display)')
    # Normal host transfer has no unconditional command-mode stop-scan call.
    for name in ('dsi_host_transfer', 'dsi_ctrl_cmd_transfer', 'dsi_message_tx'):
        p = path if name == 'dsi_host_transfer' else path.replace('dsi_display.c', 'dsi_ctrl.c')
        body = function(p, name)
        assert 'dsi_ctrl_wait_for_cmd_mode_mdp_idle(' not in body
        assert 'autorefresh(' not in body and 'control_te(' not in body
    tx = function(path.replace('dsi_display.c', 'dsi_ctrl.c'), 'dsi_message_tx')
    ordered(tx, 'if(!(flags&DSI_CTRL_CMD_DEFER_TRIGGER))',
            'reinit_completion(&dsi_ctrl->irq_info.cmd_dma_done)',
            'dsi_hw_ops.kickoff_command(', 'wait_for_completion_timeout(')
    checks.append(['host/ctrl/message command chain has no unconditional TE/autorefresh stop; dynamic clock change has an MDP-idle wait'])
    pll = 'drivers/clk/qcom/mdss/mdss-dsi-pll-7nm.c'
    ordered(function(pll, 'vco_7nm_unprepare'), 'dsi_pll_disable(vco)', 'mdss_pll_resource_enable(pll,false)')
    ordered(function(pll, 'dsi_pll_disable'), 'dsi_pll_disable_global_clk(rsc)',
            'MDSS_PLL_REG_W(rsc->phy_base,PHY_CMN_PLL_CNTRL,0)', 'dsi_pll_disable_sub(rsc)')
    ordered(function(pll, 'dsi_pll_disable_sub'), 'MDSS_PLL_REG_W(rsc->phy_base,PHY_CMN_RBUF_CTRL,0)', 'dsi_pll_disable_pll_bias(rsc)')
    ordered(function(pll, 'dsi_pll_disable_pll_bias'), 'MDSS_PLL_REG_W(rsc->pll_base,PLL_SYSTEM_MUXES,0)', 'MDSS_PLL_REG_W(rsc->phy_base,PHY_CMN_CTRL_0,data&~BIT(5))', 'ndelay(250)')
    for profile in ('pv', 'pp1', 'dv1', 'dv2'):
        p = f'arch/arm64/boot/dts/lge/sm8150_common/settings_panel/dsi-panel-r66456-{profile}-tianmamd-fhd-cmd.dtsi'
        data = source(p)
        assert 'qcom,ulps-enabled;' in data and 'qcom,suspend-ulps-enabled;' in data
        assert 'qcom,mdss-dsi-force-clock-lane-hs' not in data
    checks.append(['four target DTS files: normal/suspend ULPS enabled, forced HS lane absent; include overrides audited separately in full-flow matrix'])
    return dict(kind='static source guards/call sites, not DRM/CCF execution', sources=sources, checks=checks)
