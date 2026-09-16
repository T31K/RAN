


-- sp_purchase_change_state
CREATE Procedure [dbo].[sp_purchase_change_state]
	/* Param List */	
	@purkey varchar(22),
    @purflag int,
	@nReturn int OUTPUT
AS
	DECLARE 	
		@error_var int, -- Declare variables used in error checking.
		@rowcount_var int,
		@nFlag int

	SET NOCOUNT ON
	
	SET @nFlag = 0
	
	SELECT @nFlag=PurFlag FROM ShopPurchase WHERE PurKey=@purkey
	
	IF @error_var <> 0 OR @rowcount_var = 0
	BEGIN
	    SET @nReturn = 0
	    SET NOCOUNT OFF
	    RETURN @nReturn	
	END
	
	-- 官操妨绰 内靛客 泅犁 内靛啊 鞍篮 版快 俊矾
	IF @nFlag = @purflag
	BEGIN
	    SET @nReturn = 0
	    SET NOCOUNT OFF
	    RETURN @nReturn	
	END
	
	-- 沥犬窍霸 官曹荐 乐绰 版快
    Update ShopPurchase SET PurFlag=@purflag, PurChgDate=getdate() WHERE PurKey=@purkey

	SELECT @error_var = @@ERROR, @rowcount_var = @@ROWCOUNT
	IF @error_var <> 0 OR @rowcount_var = 0
	BEGIN
	    -- 角菩
	    SET @nReturn = 0
	END
    ELSE
    BEGIN
	    -- 己傍
	    INSERT INTO LogShopPurchase (PurKey, PurFlag) VALUES (@purkey, @purflag)
	    SET @nReturn = 1
    END

	SET NOCOUNT OFF

	RETURN @nReturn



