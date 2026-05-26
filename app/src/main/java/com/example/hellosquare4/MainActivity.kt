package com.example.hellosquare4

import android.animation.Animator
import android.animation.AnimatorListenerAdapter
import android.graphics.Color
import android.media.AudioManager
import android.media.ToneGenerator
import android.os.Bundle
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.view.animation.AccelerateDecelerateInterpolator
import android.widget.Button
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import com.google.androidgamesdk.GameActivity
import java.util.Random

class MainActivity : GameActivity() {
    private lateinit var scoreView: TextView
    private lateinit var table4View: TextView
    private lateinit var table6View: TextView
    private lateinit var winOverlay: FrameLayout
    private lateinit var outOverlay: FrameLayout
    private var count4 = 0
    private var count6 = 0
    private var isGameWon = false
    private val toneGenerator = ToneGenerator(AudioManager.STREAM_MUSIC, 100)

    private external fun nativeResetGame()

    companion object {
        init {
            System.loadLibrary("hellosquare4")
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setupOverlay()
    }

    private fun setupOverlay() {
        val root = window.decorView as ViewGroup
        val overlay = FrameLayout(this)
        overlay.layoutParams = FrameLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.MATCH_PARENT
        )

        scoreView = TextView(this).apply {
            setTextColor(Color.WHITE)
            textSize = 24f
            text = "Score: 0"
        }
        val scoreParams = FrameLayout.LayoutParams(
            FrameLayout.LayoutParams.WRAP_CONTENT,
            FrameLayout.LayoutParams.WRAP_CONTENT
        ).apply {
            gravity = Gravity.BOTTOM or Gravity.START
            setMargins(50, 0, 0, 100)
        }
        overlay.addView(scoreView, scoreParams)

        table4View = TextView(this).apply {
            setTextColor(Color.YELLOW)
            textSize = 16f
            text = "Table of 4:"
        }
        val table4Params = FrameLayout.LayoutParams(
            FrameLayout.LayoutParams.WRAP_CONTENT,
            FrameLayout.LayoutParams.WRAP_CONTENT
        ).apply {
            gravity = Gravity.TOP or Gravity.START
            setMargins(50, 100, 0, 0)
        }
        overlay.addView(table4View, table4Params)

        table6View = TextView(this).apply {
            setTextColor(Color.CYAN)
            textSize = 16f
            text = "Table of 6:"
            gravity = Gravity.END
        }
        val table6Params = FrameLayout.LayoutParams(
            FrameLayout.LayoutParams.WRAP_CONTENT,
            FrameLayout.LayoutParams.WRAP_CONTENT
        ).apply {
            gravity = Gravity.TOP or Gravity.END
            setMargins(0, 100, 50, 0)
        }
        overlay.addView(table6View, table6Params)

        // Win Overlay
        winOverlay = FrameLayout(this).apply {
            setBackgroundColor(Color.parseColor("#CC000000"))
            visibility = View.GONE
            isClickable = true
            isFocusable = true
        }
        val winContent = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER
        }
        val winText = TextView(this).apply {
            text = "CONGRATULATIONS!\nYOU ARE THE CHAMPION!"
            setTextColor(Color.parseColor("#FFD700")) // Gold color
            textSize = 32f
            gravity = Gravity.CENTER
            setPadding(0, 0, 0, 50)
        }
        val playAgainButton = Button(this).apply {
            text = "Play Another Game"
            setOnClickListener {
                resetGame()
            }
        }
        winContent.addView(winText)
        winContent.addView(playAgainButton)
        
        val contentParams = FrameLayout.LayoutParams(
            FrameLayout.LayoutParams.WRAP_CONTENT,
            FrameLayout.LayoutParams.WRAP_CONTENT
        ).apply { gravity = Gravity.CENTER }
        winOverlay.addView(winContent, contentParams)
        overlay.addView(winOverlay)

        root.addView(overlay)

        // Out Overlay
        outOverlay = FrameLayout(this).apply {
            setBackgroundColor(Color.parseColor("#CC000000"))
            visibility = View.GONE
            isClickable = true
            isFocusable = true
        }
        val outContent = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER
        }
        val outEmoji = TextView(this).apply {
            text = "😭"
            textSize = 100f
            gravity = Gravity.CENTER
        }
        val outText = TextView(this).apply {
            text = "OH NO! YOU ARE OUT!"
            setTextColor(Color.WHITE)
            textSize = 28f
            gravity = Gravity.CENTER
            setPadding(0, 20, 0, 50)
        }
        val tryAgainButton = Button(this).apply {
            text = "Try Again"
            setOnClickListener {
                resetGame()
            }
        }
        outContent.addView(outEmoji)
        outContent.addView(outText)
        outContent.addView(tryAgainButton)

        val outContentParams = FrameLayout.LayoutParams(
            FrameLayout.LayoutParams.WRAP_CONTENT,
            FrameLayout.LayoutParams.WRAP_CONTENT
        ).apply { gravity = Gravity.CENTER }
        outOverlay.addView(outContent, outContentParams)
        overlay.addView(outOverlay)
    }

    private fun resetGame() {
        nativeResetGame()
        isGameWon = false
        winOverlay.visibility = View.GONE
        outOverlay.visibility = View.GONE
        table4View.text = "Table of 4:"
        table6View.text = "Table of 6:"
        scoreView.text = "Score: 0"
        count4 = 0
        count6 = 0
    }

    private fun showSparkles() {
        val random = Random()
        val width = if (winOverlay.width > 0) winOverlay.width else 1000
        val height = if (winOverlay.height > 0) winOverlay.height else 2000
        for (i in 0 until 50) {
            val sparkle = TextView(this).apply {
                text = "✨"
                textSize = (10 + random.nextInt(20)).toFloat()
            }
            val params = FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.WRAP_CONTENT,
                FrameLayout.LayoutParams.WRAP_CONTENT
            )
            params.leftMargin = random.nextInt(width)
            params.topMargin = random.nextInt(height)
            winOverlay.addView(sparkle, params)

            sparkle.alpha = 0f
            sparkle.animate()
                .alpha(1f)
                .scaleX(1.5f)
                .scaleY(1.5f)
                .setDuration(500 + random.nextInt(1000).toLong())
                .setInterpolator(AccelerateDecelerateInterpolator())
                .setListener(object : AnimatorListenerAdapter() {
                    override fun onAnimationEnd(animation: Animator) {
                        sparkle.animate()
                            .alpha(0f)
                            .translationYBy(-200f)
                            .setDuration(1000)
                            .setListener(object : AnimatorListenerAdapter() {
                                override fun onAnimationEnd(animation: Animator) {
                                    winOverlay.removeView(sparkle)
                                }
                            })
                    }
                })
        }
    }

    private fun showWinSplash() {
        if (isGameWon) return
        isGameWon = true
        winOverlay.visibility = View.VISIBLE
        winOverlay.post {
            showSparkles()
        }
    }

    fun showTable(hit: Int, total: Int, count4In: Int, count6In: Int) {
        runOnUiThread {
            // Play "taash" (simulated with ToneGenerator for now)
            toneGenerator.startTone(ToneGenerator.TONE_PROP_BEEP, 150)

            count4 = count4In
            count6 = count6In
            scoreView.text = "Score: $total"

            if (hit == 4) {
                val table = StringBuilder("Table of 4:\n")
                for (i in 1..count4.coerceAtMost(10)) {
                    table.append("4 x $i = ${4 * i}\n")
                }
                table4View.text = table.toString()
            } else if (hit == 6) {
                val table = StringBuilder("Table of 6:\n")
                for (i in 1..count6.coerceAtMost(10)) {
                    table.append("6 x $i = ${6 * i}\n")
                }
                table6View.text = table.toString()
            }

            if (count4 >= 10 && count6 >= 10) {
                showWinSplash()
            }
        }
    }

    fun showOut(total: Int) {
        runOnUiThread {
            outOverlay.visibility = View.VISIBLE
        }
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) {
            hideSystemUi()
        }
    }

    private fun hideSystemUi() {
        val decorView = window.decorView
        decorView.systemUiVisibility = (View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                or View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                or View.SYSTEM_UI_FLAG_FULLSCREEN)
    }
}