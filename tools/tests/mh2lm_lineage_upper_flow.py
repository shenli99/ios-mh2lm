"""Static call-site audit of the pinned LG normal command modeset path.

This is deliberately separate from the manually composed low-level MMIO test.
It verifies real callers, ordering and guards, not execution of the Linux DRM
stack or equivalence of every error/continuous-splash/seamless branch.
"""
import hashlib
import re


def audit(root):
    base = root / 'drivers/gpu/drm/msm'
    sources = {}
    checks = []

    def source(path):
        text = (base / path).read_text()
        sources[path] = hashlib.sha256(text.encode()).hexdigest()
        # Remove comments and strings so a log/comment cannot satisfy a call.
        return re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"', '', text, flags=re.S)

    def function(path, name):
        text = source(path)
        match = re.search(r'\b' + name + r'\s*\([^;{}]*\)\s*\{', text)
        assert match, (path, name)
        start = match.end()
        end, depth = start, 1
        while depth:
            depth += (text[end] == '{') - (text[end] == '}')
            end += 1
        return re.sub(r'\s+', '', text[start:end-1])

    def ordered(body, *calls):
        position = 0
        for call in calls:
            at = body.find(call, position)
            assert at >= 0, call
            position = at + len(call)
        checks.append(list(calls))

    atomic = function('msm_atomic.c', 'msm_atomic_helper_commit_modeset_enables')
    ordered(atomic, 'drm_bridge_pre_enable(encoder->bridge)', 'funcs->enable(encoder)',
            'kms->funcs->commit(kms,old_state)', 'drm_bridge_enable(encoder->bridge)')
    bridge = function('dsi-staging/dsi_drm.c', 'dsi_bridge_pre_enable')
    ordered(bridge, 'dsi_display_prepare(c_bridge->display)', 'dsi_display_enable(c_bridge->display)')
    enc = function('sde/sde_encoder.c', 'sde_encoder_virt_enable')
    ordered(enc, 'phys->ops.enable(phys)', '_sde_encoder_virt_enable_helper(drm_enc)')
    helper = function('sde/sde_encoder.c', '_sde_encoder_virt_enable_helper')
    assert 'sde_encoder_control_te(drm_enc,true)' in helper
    assert '.enable=sde_encoder_virt_enable' in re.sub(r'\s+', '', source('sde/sde_encoder.c'))
    checks.append(['encoder enable helper connects TE before atomic KMS commit'])

    path = 'sde/sde_encoder_phys_cmd.c'
    for name in ('sde_encoder_phys_cmd_enable', 'sde_encoder_phys_cmd_enable_helper',
                 '_sde_encoder_phys_cmd_pingpong_config', 'sde_encoder_phys_cmd_tearcheck_config'):
        body = function(path, name)
        assert 'reset_control(' not in body and '_sde_crtc_reset_hw(' not in body
    assert 'sde_encoder_phys_cmd_enable_helper(phys_enc)' in function(path, 'sde_encoder_phys_cmd_enable')
    assert '_sde_encoder_phys_cmd_pingpong_config(phys_enc)' in function(path, 'sde_encoder_phys_cmd_enable_helper')
    assert 'sde_encoder_phys_cmd_tearcheck_config(phys_enc)' in function(path, '_sde_encoder_phys_cmd_pingpong_config')
    disabled = function(path, 'sde_encoder_phys_cmd_disable')
    assert 'enable_tearcheck(phys_enc->hw_intf,false)' in disabled
    crtc = re.sub(r'\s+', '', source('sde/sde_crtc.c'))
    assert 'if(reset_req){if(_sde_crtc_reset_hw(crtc,old_state,recovery_events))' in crtc
    checks.append(['command enable chain has no CTL reset', 'CRTC reset is guarded by reset_req', 'command disable stops tearcheck'])

    path = 'dsi-staging/dsi_display.c'
    ordered(function(path, 'dsi_display_enable'), 'dsi_panel_enable(display->panel)',
            'dsi_display_cmd_engine_enable(display)')
    ordered(function(path, 'dsi_display_disable'), 'dsi_display_cmd_engine_disable(display)',
            'dsi_panel_disable(display->panel)')
    transfer = function(path, 'dsi_host_transfer')
    ordered(transfer, 'dsi_display_cmd_engine_enable(display)', 'if(display->tx_cmd_buf==NULL)',
            'dsi_host_alloc_cmd_tx_buffer(display)', 'dsi_ctrl_cmd_transfer(',
            'dsi_display_cmd_engine_disable(display)')
    allocation = function(path, 'dsi_host_alloc_cmd_tx_buffer')
    assert 'SZ_4K' in allocation and 'MSM_SMMU_DOMAIN_UNSECURE' in allocation
    enable = function(path, 'dsi_display_cmd_engine_enable')
    disable = function(path, 'dsi_display_cmd_engine_disable')
    assert 'if(display->cmd_engine_refcount>0)' in enable
    assert 'if(display->cmd_engine_refcount>1)' in disable
    checks.append(['one retained 4KiB buffer for all ordinary host transfers', 'refcounted command engine'])
    return dict(kind='static real upper-call-site audit, not DRM execution', sources=sources, checks=checks)
