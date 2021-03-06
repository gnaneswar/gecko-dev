/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko;

import org.mozilla.gecko.menu.GeckoMenu;
import org.mozilla.gecko.menu.MenuPopup;
import org.mozilla.gecko.menu.MenuPanel;
import org.mozilla.gecko.menu.MenuItemActionBar;
import org.mozilla.gecko.widget.GeckoPopupMenu;

import android.view.Menu;
import android.view.MenuItem;
import android.view.MenuInflater;
import android.view.ViewGroup;
import android.view.LayoutInflater;
import android.widget.LinearLayout;
import android.view.View;
import android.view.ViewGroup;
import android.content.Context;
import android.widget.TextView;
import android.widget.ImageButton;
import android.widget.Button;
import android.widget.PopupWindow;
import android.util.AttributeSet;

class ActionModeCompatView extends LinearLayout implements GeckoMenu.ActionItemBarPresenter {
    private final String LOGTAG = "GeckoActionModeCompatPresenter";

    private static final int SPEC = View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED);

    private Button mTitleView;
    private ImageButton mMenuButton;
    private ViewGroup mActionButtonBar;
    private GeckoPopupMenu mPopupMenu;

    // Maximum number of items to show as actions
    private static final int MAX_ACTION_ITEMS = 4;

    private int mActionButtonsWidth = 0;

    public ActionModeCompatView(Context context) {
        super(context);
        init(context);
    }

    public ActionModeCompatView(Context context, AttributeSet attrs) {
        super(context, attrs);
        init(context);
    }

    public ActionModeCompatView(Context context, AttributeSet attrs, int style) {
        super(context, attrs, style);
        init(context);
    }

    public void init(Context context) {
        LayoutInflater.from(context).inflate(R.layout.actionbar, this);

        mTitleView = (Button) findViewById(R.id.actionmode_title);
        mMenuButton = (ImageButton) findViewById(R.id.actionbar_menu);
        mActionButtonBar = (ViewGroup) findViewById(R.id.actionbar_buttons);

        mPopupMenu = new GeckoPopupMenu(getContext(), mMenuButton);
        ((GeckoMenu) mPopupMenu.getMenu()).setActionItemBarPresenter(this);

        mMenuButton.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) {
                openMenu();
            }
        });
    }

    public void initForMode(final ActionModeCompat mode) {
        mTitleView.setOnClickListener(mode);
        mPopupMenu.setOnMenuItemClickListener(mode);
    }

    public CharSequence getTitle() {
        return mTitleView.getText();
    }

    public void setTitle(CharSequence title) {
        mTitleView.setText(title);
    }

    public void setTitle(int resId) {
        mTitleView.setText(resId);
    }

    public Menu getMenu() {
        return mPopupMenu.getMenu();
    }

    public void invalidate() {
        // onFinishInflate may not have been called yet on some versions of Android
        if (mPopupMenu != null && mMenuButton != null) {
            mMenuButton.setVisibility(mPopupMenu.getMenu().hasVisibleItems() ? View.VISIBLE : View.GONE);
        }
        super.invalidate();
    }

    /* GeckoMenu.ActionItemBarPresenter */
    @Override
    public boolean addActionItem(View actionItem) {
        final int count = mActionButtonBar.getChildCount();
        if (count >= MAX_ACTION_ITEMS) {
            return false;
        }

        int maxWidth = mActionButtonBar.getMeasuredWidth();
        if (maxWidth == 0) {
            mActionButtonBar.measure(SPEC, SPEC);
            maxWidth = mActionButtonBar.getMeasuredWidth();
        }

        // Since we don't know how many items will be added, we always reserve space for the overflow menu
        mMenuButton.measure(SPEC, SPEC);
        maxWidth -= mMenuButton.getMeasuredWidth();

        if (mActionButtonsWidth <= 0) {
            mActionButtonsWidth = 0;

            // Loop over child views, measure them, and add their width to the taken width
            for (int i = 0; i < count; i++) {
                View v = mActionButtonBar.getChildAt(i);
                v.measure(SPEC, SPEC);
                mActionButtonsWidth += v.getMeasuredWidth();
            }
        }

        actionItem.measure(SPEC, SPEC);
        int w = actionItem.getMeasuredWidth();
        if (mActionButtonsWidth + w < maxWidth) {
            // We cache the new width of our children.
            mActionButtonsWidth += w;
            mActionButtonBar.addView(actionItem);
            return true;
        }
        return false;
    }

    /* GeckoMenu.ActionItemBarPresenter */
    @Override
    public void removeActionItem(View actionItem) {
        mActionButtonsWidth -= actionItem.getMeasuredWidth();
        mActionButtonBar.removeView(actionItem);
    }

    public void openMenu() {
        mPopupMenu.openMenu();
    }

    public void closeMenu() {
        mPopupMenu.dismiss();
    }

}
